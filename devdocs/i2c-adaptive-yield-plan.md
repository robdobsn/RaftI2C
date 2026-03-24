# I2C Bus Worker Task: Adaptive Yield Scheduling Plan

## Problem Statement

The `i2cWorkerTask` in `BusI2C.cpp` uses two fixed configuration parameters to control when it yields to other FreeRTOS tasks:

- `_loopYieldEveryMs` — how long the loop runs before yielding (default 10 ms)
- `_loopYieldForMs` — how long it yields (default 1 ms)

These are set once at `setup()` from JSON config and never change. This causes two conflicting problems:

1. **Watchdog starvation**: If `_loopYieldEveryMs` is long (e.g. 20 ms) and `_loopYieldForMs` is short (e.g. 1 ms), the idle task is starved. The ESP32 Task Watchdog Timer fires because the idle task (which resets it) can't run at sufficient duty cycle.

2. **Polling latency inflated by yield overhead**: If `_loopYieldForMs` is made large enough to keep the idle task happy, the actual achievable poll interval for devices worsens by factor `(active + yield) / active`.

Neither parameter can be set correctly without knowing how many devices are being polled and at what rates — which is only known after setup.

---

## Key Insight: Yield Parameters Are Derivable

The two yield parameters are not independent configuration values — they are scheduling parameters that should be **derived from the polling workload** and the **watchdog constraint**. Specifically:

Let:
- $T_{WD}$ = watchdog timeout in ms — read from `CONFIG_ESP_TASK_WDT_TIMEOUT_S` (see below)
- $N$ = number of actively polled devices
- $f_{max}$ = highest poll frequency among all active devices (Hz), so $T_{min} = 1000/f_{max}$ ms
- $\bar{t}$ = mean I2C transaction time per device (µs) — derived from known values, not assumed (see below)

Then the **raw loop time** to service one pass over all N devices (ignoring yield) is:

$$T_{loop} = N \cdot \bar{t}$$

For correctness, the loop must complete one full pass over all devices within the fastest device's required interval, even accounting for yield overhead:

$$T_{loop} \cdot \frac{t_{active} + t_{yield}}{t_{active}} \leq T_{min}$$

Rearranging for the yield ratio:

$$\frac{t_{yield}}{t_{active}} \leq \frac{T_{min}}{T_{loop}} - 1$$

Combined with the watchdog constraint:

$$t_{active} \leq \frac{T_{WD}}{10}$$

This gives us enough information to compute both parameters from known values.

---

## Sources for Each Quantity

### Watchdog Timeout

`CONFIG_ESP_TASK_WDT_TIMEOUT_S` is an ESP-IDF Kconfig integer (seconds), available at compile time as a preprocessor macro. It is set in `sdkconfig` and reflects the actual TWDT timeout configured for the build. Use it directly:

```cpp
static const uint32_t WD_TIMEOUT_MS = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000;
static const uint32_t WD_BUDGET_MS  = WD_TIMEOUT_MS / 10;  // 10× safety margin
```

Log this at startup so it is visible if the build's watchdog config is unexpectedly different from what is expected.

### Minimum Yield Duration

The minimum meaningful yield is one FreeRTOS scheduler tick. This is available at compile time as `portTICK_PERIOD_MS` (milliseconds per tick, i.e. `1000 / CONFIG_FREERTOS_HZ`). On a standard ESP32 build with `CONFIG_FREERTOS_HZ = 1000`, this is 1 ms, but it must be read from `portTICK_PERIOD_MS` rather than assumed. Log the tick period at startup.

```cpp
static const uint32_t MIN_YIELD_MS = portTICK_PERIOD_MS;
```

### I2C Transaction Time

This is the most important quantity to get right. There are two phases:

#### Phase 1 — Bootstrap estimate (before any transactions have been measured)

`_freq` is already read from the JSON config key `i2cFreq` (default 100000 Hz) in `BusI2C::setup()` and stored as a member. Use it directly to derive a per-bit time:

$$t_{bit} = \frac{10^6}{\texttt{\_freq}} \; \mu s$$

A complete I2C transaction for a write-then-read of $W$ write bytes and $R$ read bytes consists of:

| Segment | Bit count |
|---|---|
| START | 1 |
| 7-bit address + R/W | 9 (8 + 1 ACK) |
| $W$ write data bytes | $9W$ (8 + 1 ACK each) |
| Repeated START (if read follows write) | 1 |
| 7-bit address + R/W (read direction) | 9 |
| $R$ read data bytes | $9R$ (8 + 1 ACK/NACK each) |
| STOP | 1 |

Total bits = $21 + 9W + 9R$ for a combined write-read, or $11 + 9W$ for write-only.

The number of bytes per transaction is device-specific and is known per-device (it is encoded in the poll request records in `BusRequestInfo`). `DevicePollingMgr` iterates these; the per-device write and read lengths are available there. The bootstrap estimate should compute a sum over all devices' poll request byte counts rather than assuming a fixed payload:

```cpp
// In DevicePollingMgr — compute estimated total transaction time across all devices
uint32_t estimateTotalXactUs(uint32_t freqHz) const
{
    uint32_t totalUs = 0;
    for (each polling device)
    {
        for (each BusRequestInfo in the device's pollReqs)
        {
            uint32_t W = busReqRec.getWriteDataLen();
            uint32_t R = busReqRec.getReadDataLen();
            uint32_t bits = (R > 0) ? (21 + 9*W + 9*R) : (11 + 9*W);
            totalUs += (bits * 1000000UL) / freqHz;
        }
    }
    return totalUs;
}
```

If a device has no poll requests yet registered (N > 0 but no byte count available), log `LOG_W` clearly:

```
LOG_W(MODULE_PREFIX, "recomputeYieldParams: device at addr 0x%02x has no poll requests yet - using 1-byte estimate for loop time calculation");
```

#### Phase 2 — Measured (preferred, replaces bootstrap after sufficient data)

`DevicePollingMgr::taskService()` already calls `_busReqSyncFn` for each transaction. Wrap these calls with `micros()` timestamps to accumulate a per-device exponential moving average of actual transaction time:

```cpp
uint64_t startUs = micros();
auto rslt = _busReqSyncFn(&busReqRec, &readData);
uint64_t xactUs = micros() - startUs;
// Exponential moving average, weight 7/8 old + 1/8 new
_deviceMeanXactUs[i] = (_deviceMeanXactUs[i] * 7 + xactUs) / 8;
```

`DevicePollingMgr` then exposes:

```cpp
// Returns sum of mean transaction times across all registered polling devices (µs).
// Returns 0 if no measurements have been taken yet.
uint32_t getMeasuredTotalXactUs() const;
```

`recomputeYieldParams()` uses `getMeasuredTotalXactUs()` when it returns > 0, falling back to the bootstrap estimate otherwise. Both paths log which source is being used.

---

## Recommended Architecture

### Step 1: Add `getMinPollIntervalMs()` and `getActivePollCount()` to `BusStatusMgr`

`BusStatusMgr` already tracks all polled devices and their rates (transitively via `BusI2CScheduler`). It should expose:

```cpp
// Returns the minimum poll interval across all active polling devices, in ms.
// Returns UINT32_MAX if no devices are being polled.
uint32_t getMinPollIntervalMs() const;

// Returns the number of devices with active polling registrations.
uint32_t getActivePollCount() const;
```

These are cheaply maintained as running state — updated when a device is added, removed, or its rate changes. Note that `BusI2CScheduler::_pollMinTimeMs` already computes the minimum poll interval; `getMinPollIntervalMs()` simply exposes it.

### Step 2: Add `recomputeYieldParams()` to `BusI2C`

This private method recomputes `_loopYieldEveryMs` and `_loopYieldForMs` from the known quantities above:

```cpp
void BusI2C::recomputeYieldParams()
{
    // --- Known quantities, not assumed ---

    // Watchdog timeout from IDF Kconfig (compile-time constant)
    static const uint32_t WD_TIMEOUT_MS = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000;
    static const uint32_t WD_BUDGET_MS  = WD_TIMEOUT_MS / 10;

    // Minimum yield is one FreeRTOS tick (portTICK_PERIOD_MS, compile-time)
    static const uint32_t MIN_YIELD_MS = portTICK_PERIOD_MS;

    uint32_t N = _busStatusMgr.getActivePollCount();
    uint32_t minPollMs = _busStatusMgr.getMinPollIntervalMs();

    // No active polling — use safe defaults and log
    if (N == 0 || minPollMs == UINT32_MAX)
    {
        _loopYieldEveryMs = I2C_BUS_LOOP_YIELD_EVERY_MS;
        _loopYieldForMs   = I2C_BUS_LOOP_YIELD_FOR_MS;
        LOG_I(MODULE_PREFIX, "recomputeYieldParams no active polling - defaults: yieldEvery=%u yieldFor=%u WD_TIMEOUT=%ums tickPeriod=%ums",
              _loopYieldEveryMs, _loopYieldForMs, WD_TIMEOUT_MS, portTICK_PERIOD_MS);
        return;
    }

    // Total loop time: prefer measured, fall back to bootstrap estimate from _freq and byte counts
    uint32_t loopTimeUs = _devicePollingMgr.getMeasuredTotalXactUs();
    bool usingMeasured = (loopTimeUs > 0);
    if (!usingMeasured)
    {
        loopTimeUs = _devicePollingMgr.estimateTotalXactUs(_freq);
        LOG_W(MODULE_PREFIX, "recomputeYieldParams using bootstrap xact time estimate (no measurements yet) - loopTimeUs=%u freq=%u",
              loopTimeUs, _freq);
    }

    uint32_t loopTimeMs = (loopTimeUs + 999) / 1000;   // ceiling in ms

    // t_active: as large as possible, bounded by watchdog budget and minPollMs/2
    uint32_t tActive = std::min(WD_BUDGET_MS, minPollMs / 2);
    tActive = std::max(tActive, MIN_YIELD_MS);

    // t_yield: from polling constraint floor, raised to watchdog floor
    // Polling constraint: t_yield <= t_active * (T_min/T_loop - 1)
    uint32_t tYield = MIN_YIELD_MS;
    if (loopTimeMs > 0 && minPollMs > loopTimeMs)
    {
        uint32_t pollBudgetYield = tActive * (minPollMs - loopTimeMs) / loopTimeMs;
        tYield = std::min(tYield, pollBudgetYield);
    }
    // Watchdog floor: idle task must run at least once per WD_TIMEOUT_MS.
    // Worst case: idle task gets a slice every (tActive + tYield) ms.
    // Require: tYield >= portTICK_PERIOD_MS (already set) and tYield/tActive not too small.
    // A single tick yield every tActive ms is sufficient as long as tActive << WD_TIMEOUT_MS,
    // which is guaranteed by WD_BUDGET_MS = WD_TIMEOUT_MS/10.
    tYield = std::max(tYield, MIN_YIELD_MS);

    _loopYieldEveryMs = tActive;
    _loopYieldForMs   = tYield;

    LOG_I(MODULE_PREFIX, "recomputeYieldParams N=%u minPollMs=%u loopTime=%u%s tActive=%u tYield=%u WD_TIMEOUT=%u tickPeriod=%u",
          N, minPollMs, usingMeasured ? loopTimeUs : loopTimeUs,
          usingMeasured ? "us(measured)" : "us(estimated)",
          tActive, tYield, WD_TIMEOUT_MS, portTICK_PERIOD_MS);
}
```

### Step 3: Call `recomputeYieldParams()` at the Right Times

Trigger points (all executed either before the task starts, or with the task paused, to avoid races on `_loopYieldEveryMs`/`_loopYieldForMs`):

| Event | Where to hook |
|---|---|
| `BusI2C::setup()` complete | After all devices from config are registered |
| Device added to polling list | `BusStatusMgr` — after inserting poll registration |
| Device removed / disconnected | `BusStatusMgr` — after removing poll registration |
| Device poll rate changed | `BusStatusMgr` — after updating rate |
| Sufficient measurements accumulated | `DevicePollingMgr` — after N samples, triggers callback to `BusI2C` |

Because `_loopYieldEveryMs` and `_loopYieldForMs` are read only in `i2cWorkerTask`, and writes happen either during setup (before task starts) or while the task is paused, no additional synchronisation is needed beyond the existing pause mechanism.

---

## Watchdog Safety Reasoning

The TWDT on ESP32 fires if the idle task has not run for `CONFIG_ESP_TASK_WDT_TIMEOUT_S` seconds. With `t_active = WD_TIMEOUT_MS / 10` and `t_yield = portTICK_PERIOD_MS`, the worst-case gap between idle-task opportunities is:

$$t_{active} + t_{yield} = \frac{T_{WD}}{10} + t_{tick}$$

Since $t_{tick} \ll T_{WD}$, this is well within $T_{WD}$. The 10× safety margin is conservative; the actual requirement is only that `t_active < T_WD - t_tick`, but 10× leaves room for scheduling jitter. Both `T_WD` and `t_tick` are logged at startup so any unexpected values are immediately visible.

### Hard Ceiling on `_loopYieldEveryMs`

The adaptive path naturally keeps `t_active ≤ WD_BUDGET_MS = WD_TIMEOUT_MS / 10`, so the TWDT cannot fire through adaptive-computed values. However, the manual override path (and any bug in `recomputeYieldParams()`) could set `_loopYieldEveryMs` to a dangerously large value.

Therefore, a hard ceiling must be enforced **directly in the worker loop**, independent of how `_loopYieldEveryMs` was set:

```cpp
// In i2cWorkerTask() — applied unconditionally, before using _loopYieldEveryMs:
static const uint32_t WD_TIMEOUT_MS = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000;
static const uint32_t MAX_SAFE_YIELD_EVERY_MS = WD_TIMEOUT_MS / 10;

// Clamp at startup and whenever _loopYieldEveryMs is changed:
if (_loopYieldEveryMs > MAX_SAFE_YIELD_EVERY_MS)
{
    LOG_E(MODULE_PREFIX, "i2cWorkerTask _loopYieldEveryMs=%u exceeds WD safe limit %u - clamping",
          _loopYieldEveryMs, MAX_SAFE_YIELD_EVERY_MS);
    _loopYieldEveryMs = MAX_SAFE_YIELD_EVERY_MS;
}
```

The clamping is best placed in a setter or at the top of `recomputeYieldParams()`, applied to the final value before it is stored, so the enforcement point is single and auditable. It must run even when the manual override path is taken.

---

## Scan Unyield Parameters (`_loopFastUnyieldUs` / `_loopSlowUnyieldUs`)

### What These Parameters Actually Control

These are separate from the main yield parameters. They control the **per-call scanning time budget** passed to `BusScanner::taskService()` each time it is invoked from the worker loop. The scanner uses whichever value is appropriate for its current mode:

```cpp
// In BusScanner::taskService() — the loop-exit condition:
if (sweepCompleted || Raft::isTimeout(micros(), scanLoopStartTimeUs,
        _scanMode == SCAN_MODE_SCAN_FAST ? maxFastTimeInLoopUs : maxSlowTimeInLoopUs))
    break;
```

So they answer the question: *"within each worker-loop iteration, how many µs are donated to scanning?"*

### Scanning Mode Lifecycle

The scanner progresses through states automatically:

```
IDLE → SCAN_MODE_MAIN_BUS_MUX_ONLY → SCAN_MODE_MAIN_BUS → SCAN_MODE_SCAN_FAST → SCAN_MODE_SCAN_SLOW
```

- **IDLE / MUX_ONLY / MAIN_BUS**: startup phases; `isScanPending()` always returns true; `maxFastTimeInLoopUs` budget used.
- **SCAN_FAST**: runs flat-out (no inter-call delay); objective is to sweep all addresses as fast as possible on entry; uses `maxFastTimeInLoopUs`.
- **SCAN_SLOW**: rate-limited by `_slowScanPeriodMs` between calls; ongoing background detection of attach/detach; uses `maxSlowTimeInLoopUs`.

### Deriving the Values

A probe transaction (scanning — no data payload, just address ACK/NACK) consists of:

| Segment | Bit count |
|---|---|
| START | 1 |
| 7-bit address + R/W | 9 |
| ACK/NACK | already included above |
| STOP | 1 |

Total: **11 bits**. Using `_freq` (already known from config):

$$t_{probe\_us} = \frac{11 \times 10^6}{\texttt{\_freq}}$$

The total number of scan addresses per sweep, $A_{total}$, is the sum of all entries across all `_scanPriorityLists` plus the full 112-address main-bus sweep. This is known from `BusScanner` after `setup()`.

#### Fast Phase Budget

The goal during FAST is to complete a full address sweep as quickly as possible. The ideal per-call budget is one full sweep's worth:

$$maxFastTimeInLoopUs = A_{total} \times t_{probe\_us}$$

This can't exceed the outer loop's active window (`_loopYieldEveryMs * 1000`) since that's how long the loop runs before yielding. Cap accordingly. If the full sweep fits in one call, FAST phase completes in one iteration; otherwise it spans multiple iterations, each consuming the full budget.

#### Slow Phase Budget

The goal during SLOW is to detect any device attach/detach within a desired detection latency $D_{ms}$. Given:
- $A_{total}$ total addresses to sweep
- $t_{period\_ms}$ = `_slowScanPeriodMs` (inter-call gap, defaults to 5 ms)
- $t_{probe\_us}$ per address

The scan completes a full sweep in $\lceil A_{total} \times t_{probe\_us} / maxSlowTimeInLoopUs \rceil$ calls, taking approximately:

$$D_{sweep\_ms} = \left\lceil \frac{A_{total} \times t_{probe\_us}}{maxSlowTimeInLoopUs} \right\rceil \times t_{period\_ms}$$

To achieve a target detection latency $D_{target\_ms}$ (the time from when a device connects or disconnects to when the firmware notices):

$$maxSlowTimeInLoopUs = \frac{A_{total} \times t_{probe\_us} \times t_{period\_ms}}{D_{target\_ms}}$$

$D_{target\_ms}$ should be a config parameter (e.g. `slowScanDetectLatencyMs`) with a sensible default (e.g. 5000 ms — detecting a newly connected device within 5 seconds is usually adequate).

### What to Keep Configurable

The existing config keys `fastScanMaxUnyieldMs` and `slowScanMaxUnyieldMs` become manual overrides, consistent with the approach for the main yield parameters. Always log when they are used:

```cpp
bool hasManualFast = config.contains("fastScanMaxUnyieldMs");
bool hasManualSlow = config.contains("slowScanMaxUnyieldMs");

if (hasManualFast)
{
    _loopFastUnyieldUs = config.getLong("fastScanMaxUnyieldMs", I2C_BUS_FAST_MAX_UNYIELD_DEFAUT_MS) * 1000;
    LOG_W(MODULE_PREFIX, "setup fast scan unyield manually overridden: %uus - adaptive disabled for this parameter",
          _loopFastUnyieldUs);
}
// ... similarly for slow
```

The `slowScanDetectLatencyMs` config key drives the adaptive slow path, defaulting to a value that produces reasonable detection latency on a typical bus. Its default and the computed `maxSlowTimeInLoopUs` are always logged.

### Interaction With Main Yield Parameters

Note that `maxFastTimeInLoopUs` is bounded by `_loopYieldEveryMs * 1000`: the scanner cannot usefully be given more time in a single iteration than the total active window. The implementation should enforce:

```cpp
_loopFastUnyieldUs = std::min(_loopFastUnyieldUs, _loopYieldEveryMs * 1000u);
```

This is also applied after any change to `_loopYieldEveryMs`.

### Summary Table of Derivations

| Parameter | Derived from | Default if no data yet |
|---|---|---|
| `maxFastTimeInLoopUs` | `A_total × t_probe_us` (`_freq`, scan list sizes from `BusScanner`) | `I2C_BUS_FAST_MAX_UNYIELD_DEFAUT_MS * 1000` |
| `maxSlowTimeInLoopUs` | `A_total × t_probe_us × _slowScanPeriodMs / D_target_ms` | `I2C_BUS_SLOW_MAX_UNYIELD_DEFAUT_MS * 1000` |
| `_loopYieldEveryMs` | `min(WD_TIMEOUT_MS/10, T_min/2)` | `I2C_BUS_LOOP_YIELD_EVERY_MS` |
| `_loopYieldForMs` | polling constraint + WD floor using `portTICK_PERIOD_MS` | `I2C_BUS_LOOP_YIELD_FOR_MS` |

---

## Scheduler Alignment

`BusI2CScheduler` already computes `_pollMinTimeMs` (the minimum time between polls of the fastest device). This is the $T_{min}$ used above; `getMinPollIntervalMs()` should return it directly.

The key feasibility invariant is:

$$T_{loop} \leq T_{min}$$

where $T_{loop}$ is the raw loop time (sum of all transaction times, no yield overhead). If this is violated, the bus is geometrically overloaded — no yield tuning can fix it. `recomputeYieldParams()` must `LOG_E` in this case:

```cpp
if (loopTimeMs >= minPollMs)
{
    LOG_E(MODULE_PREFIX, "recomputeYieldParams BUS OVERLOADED: loopTimeMs=%u >= minPollMs=%u for N=%u devices - polling rates cannot be met",
          loopTimeMs, minPollMs, N);
}
```

---

## Config Parameters: What to Keep

The existing config keys `loopYieldEveryMs` and `loopYieldForMs` become **manual overrides**: if present in JSON config, the adaptive logic is bypassed entirely. This preserves backward compatibility and allows manual tuning. Their presence is always logged.

Suggested logic in `setup()`:

```cpp
bool hasManualYieldEvery = config.contains("loopYieldEveryMs");
bool hasManualYieldFor   = config.contains("loopYieldForMs");
if (hasManualYieldEvery || hasManualYieldFor)
{
    _loopYieldEveryMs = config.getLong("loopYieldEveryMs", I2C_BUS_LOOP_YIELD_EVERY_MS);
    _loopYieldForMs   = config.getLong("loopYieldForMs",   I2C_BUS_LOOP_YIELD_FOR_MS);
    _adaptiveYield    = false;
    LOG_W(MODULE_PREFIX, "setup yield params manually overridden: yieldEvery=%u yieldFor=%u - adaptive scheduling disabled",
          _loopYieldEveryMs, _loopYieldForMs);
}
else
{
    _adaptiveYield = true;
    recomputeYieldParams();   // initial computation (N=0, logs defaults)
}
```

---

## Summary of Changes Required

| File | Change |
|---|---|
| `BusStatusMgr.h/.cpp` | Add `getMinPollIntervalMs()` (delegates to `BusI2CScheduler::_pollMinTimeMs`) and `getActivePollCount()` |
| `DevicePollingMgr.h/.cpp` | Add `estimateTotalXactUs(uint32_t freqHz)` using `BusRequestInfo` byte counts; add `getMeasuredTotalXactUs()` from per-transaction EMA timing; trigger `recomputeYieldParams` callback after N measurements |
| `BusScanner.h/.cpp` | Add `getScanAddressCount()` returning total addresses across all priority lists; add `getProbeTxTimeUs(uint32_t freqHz)` returning per-probe bit-time; expose `_slowScanPeriodMs` via getter |
| `BusI2C.h` | Add `_adaptiveYield` flag; add `recomputeYieldParams()` and `recomputeScanUnyieldParams()` declarations; add `_slowScanDetectLatencyMs` member |
| `BusI2C.cpp` | Implement `recomputeYieldParams()` using `CONFIG_ESP_TASK_WDT_TIMEOUT_S`, `portTICK_PERIOD_MS`, and `_freq`; implement `recomputeScanUnyieldParams()` using `BusScanner` address count and probe time; enforce watchdog hard ceiling on `_loopYieldEveryMs`; clamp `_loopFastUnyieldUs` to `_loopYieldEveryMs * 1000`; make all four parameters manual-config overrides with `LOG_W`; add `slowScanDetectLatencyMs` config key |

No changes are needed to `BusI2CScheduler` — it already computes the minimum poll interval; it just needs to be exposed via `BusStatusMgr`.
