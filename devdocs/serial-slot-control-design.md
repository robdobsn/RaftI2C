# Serial Slot Control — Design and Implementation Plan

Date: 2026-04-28
Status: Draft / planning (no code changes yet)

## 1. Goal

On Axiom hardware (e.g. systype `Axiom009`) the I2C "slots" exposed by the
`BusI2C` multiplexer are not pure I2C — each physical slot has electrical
hardware that can be reconfigured between three modes:

1. **I2C mode** — the SDA/SCL lines are routed through the bus multiplexer
   (PCA9548A‑style) and behave as a standard I2C slot.
2. **Serial full‑duplex mode** — the slot's pins are repurposed (via a
   hardware switch driven from a virtual GPIO on an I/O expander) so that
   one pin is the ESP32's UART RX (input) and the other is the UART TX
   (output). RX and TX run concurrently. Used for ordinary RS‑232‑style
   peripherals.
3. **Serial half‑duplex mode** — the slot is electrically (or logically)
   one shared TX/RX line. The firmware must explicitly toggle between
   "transmit" and "receive" states around every transaction: drive TX,
   send the request, then turn TX off / drain the echo, then enable the
   receiver and read the reply. Required for protocols such as the
   Waveshare serial servo bus
   ([protocol manual](https://files.waveshare.com/upload/2/27/Communication_Protocol_User_Manual-EN%28191218-0923%29.pdf))
   and other 1‑wire serial servo / smart‑actuator buses.

The current support for "serial mode" in this codebase is **experimental**:
[`DeviceSlotControl`](../../components/DeviceSlotControl/DeviceSlotControl.cpp)
just toggles the TX line at 1 Hz to verify wiring. There is **no real serial
communications path**, no integration with `BusI2C` (so I2C scanning still
runs on a slot that is electrically a UART), and the REST API is bolted onto
the device class instead of using the central [`DeviceManager`](../../../RaftCore/components/core/DeviceManager/DeviceManager.cpp)
endpoint.

This document describes the target design and an incremental implementation
plan to address those gaps.

## 2. Hardware model (from `Axiom009/SysTypes.json`)

The `DevMan/Devices` array contains a `SlotControl` entry of type
`AxiomSlotsV1`. Its `slots` array has one entry per physical slot, **indexed
from 1** (slot 1 is `slots[0]`, slot 5 is `slots[4]`):

```json
{
    "class": "SlotControl",
    "name": "SlotControl",
    "type": "AxiomSlotsV1",
    "slots": [
        { "serEnPin": 108, "serEnLevel": 0, "rxPin": 12, "txPin": 13, "defaultMode": "i2c" },
        { "serEnPin": 109, "serEnLevel": 0, "rxPin": 14, "txPin": 15, "defaultMode": "i2c" },
        { "serEnPin": 110, "serEnLevel": 0, "rxPin": 42, "txPin": 41, "defaultMode": "i2c" },
        { "serEnPin": 111, "serEnLevel": 0, "rxPin": 39, "txPin": 40, "defaultMode": "i2c" },
        { "serEnPin": 112, "serEnLevel": 0, "rxPin": 17, "txPin": 38, "defaultMode": "i2c" }
    ]
}
```

Per-slot fields:

| Field        | Meaning |
|--------------|---------|
| `serEnPin`   | GPIO or **virtual** GPIO (numbers ≥ 100 are I/O‑expander virtual pins) which, when driven to `serEnLevel`, switches the slot's mux pins from I2C to serial signalling. |
| `serEnLevel` | The level on `serEnPin` that **enables serial** (0 in Axiom009). The opposite level enables I2C. |
| `rxPin`      | ESP32‑S3 GPIO connected to the slot's RX line (input to ESP32) when in serial mode. |
| `txPin`      | ESP32‑S3 GPIO connected to the slot's TX line (output from ESP32) when in serial mode. |
| `defaultMode` | String selecting the slot's mode at boot. Accepted values: `"i2c"` (default if absent), `"full"` (boot in `SerialFull`), `"half"` (boot in `SerialHalf`). Any other value is treated as a config error and falls back to `"i2c"` with a warning log. |

> **Backward compatibility.** Earlier JSON used a boolean `serDefault`
> (`0` = I2C at boot, non‑zero = serial at boot). The parser will accept
> `serDefault` as a one‑release fallback: `serDefault: 0` maps to
> `defaultMode: "i2c"` and `serDefault: <non‑zero>` maps to
> `defaultMode: "full"`. New SysTypes.json files should write
> `defaultMode` and omit `serDefault`. There is no separate `serMode`
> field — the boot mode and the boot serial sub‑mode are the same
> choice and `defaultMode` carries both.

> **Note on naming.** The JSON field is `serEnPin` (not `serEnVPin`). It may
> hold a real ESP32 GPIO **or** an I/O‑expander virtual pin number; the
> [`RaftBusSystem::virtualPinsSet`](../../../RaftCore/components/core/Bus/RaftBusSystem.cpp)
> abstraction routes either kind transparently. The C++ struct member is
> `_serialEnPin` to match the JSON.
>
> Earlier experimental code (now superseded — see §8) read the wrong field
> name (`serEnVPin`) and had a slot off‑by‑one bug accessing
> `_slotRecs[slotNum]` instead of `_slotRecs[slotNum-1]`. Both have been
> corrected ahead of this design's implementation.
>
> Also: the boot‑default field is currently never read by the existing
> code — Phase 1 will honour `defaultMode` (with `serDefault` accepted
> as a fallback for one release).

## 3. Existing building blocks already in the codebase

The good news is most of the primitives needed are already in place — what
is missing is the orchestration.

### 3.1 Slot data‑gating in `BusI2C`

[`BusMultiplexers::enableSlot(slotNum, enableData)`](../components/RaftI2C/BusI2C/BusMultiplexers.cpp)
already manipulates a per‑mux `disabledSlotsMask`. When a slot bit is set in
that mask, [`writeSlotMaskToMux`](../components/RaftI2C/BusI2C/BusMultiplexers.cpp)
masks it out of every subsequent mux write:

```cpp
slotMask &= ~busMux.disabledSlotsMask;
```

So once a slot is disabled it is effectively invisible to scans, polls and
ad‑hoc transactions — no I2C traffic is ever steered to it. We will reuse
this exact mechanism to gate slots that are in serial mode.

The public surface is
[`RaftBusSystem::enableSlot(busName, slotNum, enablePower, enableData)`](../../../RaftCore/components/core/Bus/RaftBusSystem.cpp)
— calling it with `enableData=false` is sufficient to remove the slot from
all I2C activity (and `enablePower` can be left `true` so that the connected
serial device is still powered).

### 3.2 Virtual pin abstraction

The `serEnPin` may be a real ESP32 GPIO or a virtual pin on an I/O expander
(numbers 100+ in Axiom009 are virtual). [`RaftBusSystem::virtualPinsSet`](../../../RaftCore/components/core/Bus/RaftBusSystem.cpp)
already iterates registered buses and falls back to `digitalWrite` for plain
GPIOs, so a single API handles both cases. `DeviceSlotControl` already uses
this for its experimental code.

### 3.3 Serial bus implementation

[`BusSerial`](../../../RaftCore/components/core/Bus/BusSerial.cpp) already
exists in RaftCore and is a full `RaftBus` subclass that wraps an ESP‑IDF
UART driver:

- Constructor `BusSerial::createFn` is registered the same way as `BusI2C`.
- `setup()` reads `uartNum`, `rxPin`, `txPin`, `baudRate`, `name`,
  `rxBufSize`, `txBufSize`, `minAfterSendMs` from the bus config.
- `addRequest()` writes a buffer via `uart_write_bytes`.
- `rxDataBytesAvailable()` / `rxDataGet()` provide non‑blocking RX.

`BusSerial` is **not currently registered** by Axiom's `main.cpp` — only
`"I2C"` is. It is also not currently used by any sysmod in this project.
There is no auto‑identification / `DeviceTypeRecord` machinery for serial
devices yet (these are I2C‑specific concepts in `BusI2C`).

### 3.4 DeviceManager REST surface

[`DeviceManager::addRestAPIEndpoints`](../../../RaftCore/components/core/DeviceManager/DeviceManager.cpp)
already exposes a unified `devman/...` endpoint group covering type info,
raw commands, JSON commands, per‑device polling configuration, demo
devices, and friendly‑name assignment. This is the right place to add slot
mode/rate control — replacing the bespoke `slotcontrol/...` endpoint owned
by `DeviceSlotControl`.

The existing `devman/devconfig` already accepts `intervalUs`, `numSamples`,
`sampleRateHz`, `busHz`, `busHzSlots` and is keyed off a `deviceid` (or
`bus`+`addr`). The "rate" portion of the user's requirement therefore
overlaps significantly with code that already exists.

## 4. Target architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│ DeviceManager  (RaftCore)                                            │
│   REST API: devman/slot/...                                          │
│   - apiDevManSlot()                                                  │
│       routes to a SlotController* obtained from RaftBusSystem        │
└─────────────┬─────────────────────────────────────────────┬──────────┘
              │ getSlotController("I2CA")                   │
              ▼                                             ▼
┌──────────────────────────────────────┐   ┌───────────────────────────┐
│ SlotController (new, in RaftI2C)     │   │ DeviceManager devconfig   │
│  - per-slot state machine            │   │ (existing — used for rate)│
│      I2C ⇄ SerialFull ⇄ SerialHalf  │   └─────────────────────────┘
│  - owns a vector of slot records     │
│      driven from SysTypes "slots"    │
│  - drives serEnPin via               │
│      raftBusSystem.virtualPinsSet()  │
│  - calls bus->enableSlot(n,P,D)      │
│      (data=false in serial mode)     │
│  - allocates a BusSerial for serial  │
│      slots from a UART pool          │
└─────────────┬────────────────────────┘
              │ allocates/releases
              ▼
┌──────────────────────────────────────┐
│ UART pool (new, small helper)        │
│  - derives candidates from           │
│      SOC_UART_HP_NUM                 │
│  - skips CONFIG_ESP_CONSOLE_UART_NUM │
│  - skips uart_is_driver_installed()  │
│  - returns failNoUartAvailable       │
│      when exhausted (see §6.1)       │
└─────────────┬────────────────────────┘
              ▼
┌──────────────────────────────────────┐
│ BusSerial (existing, RaftCore)       │
│  - one instance per active serial    │
│      slot (e.g. "I2CA.s1")           │
│  - registered in raftBusSystem so    │
│      higher layers see it as a bus   │
└─────────────┬────────────────────────┘
              ▼
┌──────────────────────────────────────┐
│ Serial device drivers                │
│  - SerialDeviceTypeRecord (new)      │
│      protocol/baud/framing/poll/...  │
│  - per-protocol decoder, e.g.        │
│      WaveshareSerialServo            │
└──────────────────────────────────────┘
```

### 4.1 Where the new code lives

- **`RaftI2C/components/RaftI2C/BusI2C/SlotController.{h,cpp}`** — new.
  Owns slot mode state and the link between virtual‑pin toggling and
  `enableSlot(...,enableData=false)`. Lives in RaftI2C because the slot
  concept is inherently I2C‑mux‑centric. `BusI2C` constructs and owns one
  `SlotController` (config taken from a new `slotControl` sub‑object of the
  bus config — see §6).

- **`RaftI2C/components/RaftI2C/SerialSlots/SerialUartPool.{h,cpp}`** —
  new. Runtime UART allocator built on `SOC_UART_HP_NUM` and
  `uart_is_driver_installed()` (see §6.1).

- **`RaftI2C/components/RaftI2C/SerialSlots/SerialSlotBus.{h,cpp}`** —
  new thin wrapper that owns one `BusSerial` per active serial slot,
  exposes a stable bus name (e.g. `"I2CA.s1"`), and routes commands /
  responses through the device driver layer.

- **`RaftCore/components/core/DeviceTypes/SerialDeviceTypeRecord.{h}`** —
  new (proposed; deferred to a later phase). An extension of the existing
  `DeviceTypeRecord` schema for serial devices. See §7.

- **`RaftCore` `DeviceManager`** — gain new REST sub‑commands under the
  existing `devman` endpoint (no new endpoint registration needed):
  - `devman/slot?bus=<busName>&slot=<n>&mode=<i2c|serial>` — set mode.
  - `devman/slot?bus=<busName>&slot=<n>` — query current mode + status.
  - `devman/slot/list?bus=<busName>` — enumerate slot configs.
  - `devman/slot/cmd?bus=<busName>&slot=<n>&hexWr=<...>&numToRd=<n>` —
    raw serial transaction (mirrors `devman/cmdraw` for I2C).

  Rate control continues to use the existing `devman/devconfig`.

- **Removal**: `components/DeviceSlotControl/` is deleted from the
  application once the new pipeline is functional. Its responsibilities
  split as follows:

  | Old responsibility                       | New home |
  |------------------------------------------|----------|
  | Read `slots[]` config                    | `SlotController` (RaftI2C) |
  | Drive `serEnPin` virtual GPIO            | `SlotController` |
  | Boot‑time defaults from `defaultMode`    | `SlotController::setup()` |
  | `slotcontrol/<n>/serial|i2c` REST        | `devman/slot?...` |
| `slotcontrol/setrate/<n>/<hz>?addr=...`  | **Removed.** Superseded by `devman/devconfig` (see §4.3 below). |
| TX‑pin waggling test                     | Removed (or guarded behind a `devman/slot/test` debug command) |

### 4.3 Rate control: `slotcontrol/setrate` is obsolete

Git history confirms `devman/devconfig` is the newer mechanism and supersedes
`slotcontrol/setrate`:

| Endpoint | First commit | Last commit | Notes |
|---|---|---|---|
| `slotcontrol/setrate/<n>/<hz>?addr=` | `f5e462a` 2025‑09‑09 (`added setrate api`) | `7abf1d1` 2026‑02‑23 | Sets per‑device poll interval, filtered by slot and optional I2C address. Hardcoded to bus name `"I2CA"`. |
| `devman/devconfig?...&intervalUs=&sampleRateHz=&busHz=&busHzSlots=` | `1a66d21` 2026‑02‑21 (`Changed devman/setpollinterval to devman/devconfig`, building on earlier `setpollinterval`) | active | Bus‑agnostic, accepts `deviceid` or `bus`+`addr`, also covers bus speed and `busHzSlots`. |

`devman/devconfig` is strictly more capable (bus‑agnostic, supports bus speed,
uses the canonical device addressing model) and is the design point going
forward. **Action: delete the `slotcontrol/setrate` branch from
`DeviceSlotControl::apiControl` (and its description string) as part of
Phase 1.** No `devman` change is needed for rate — it already does the job.
## 5. Slot state machine

A slot has three modes — `I2C`, `SerialFull`, `SerialHalf`. Transitions
between any two modes always go via a brief intermediate "signalling
off" step that drops mux data and de‑asserts `serEnPin` so that two
electrical regimes are never live at the same time.

```
            +---------+
            |   I2C   |  (serEnPin=!serEnLevel,
            |         |   enableSlot(n,P=true,D=true))
            +----+----+
                 ^
        i2c <----+----> serial(half|full)
                 v
         +---------------+
         | SignallingOff |  transient: mux data off,
         | (transient)   |  serEnPin=!serEnLevel,
         +---+---+-------+  no UART installed yet
             |   |
   serFull   |   |  serHalf
             v   v
   +-----------+ +-----------+
   |SerialFull | |SerialHalf |
   |UART RX+TX | |UART TX/RX |
   |concurrent | |alternating|
   +-----------+ +-----------+
```

Transition sequences (each step is a no‑op if already in that state):

```
I2C        -> SerialFull / SerialHalf:
  1. enableSlot(n, P=true, D=false)             // mask off mux data
  2. virtualPinsSet(serEnPin, serEnLevel)        // analog switch -> serial
  3. uart = uartPool.allocate()
     if no UART -> error: revert steps 1+2
  4. busSerial = new BusSerial(uart, rxPin, txPin, baud, duplex=full|half)
  5. raftBusSystem registers it as "<busName>.s<n>"

SerialFull <-> SerialHalf:
  1. tear down BusSerial, free UART (release back to pool)
  2. (serEnPin stays at serEnLevel; mux data stays off)
  3. uart = uartPool.allocate()                  // may be the same one
  4. busSerial = new BusSerial(...,  duplex=<new>)
  5. re-register under "<busName>.s<n>"

SerialFull / SerialHalf -> I2C:
  1. tear down BusSerial, free UART
  2. virtualPinsSet(serEnPin, !serEnLevel)
  3. enableSlot(n, P=true, D=true)
```

### 5.1 Half‑duplex transmit/receive sequencing

In `SerialHalf` mode the firmware models each transaction as a strict
request→response cycle. `BusSerial` cannot today express this on its own
— it is a streaming half/full‑agnostic UART wrapper. To make half‑duplex
work we add a thin `HalfDuplexSerialChannel` adapter (Phase 2) that
encapsulates the cycle:

```
for each request:
    1. uart_flush_input()                          // discard stale RX
    2. (optional) drive direction‑enable pin -> TX // see below
    3. uart_write_bytes(req)
    4. uart_wait_tx_done(timeout)                  // ensure last bit out
    5. (optional) drive direction‑enable pin -> RX
    6. discard <reqLen> echo bytes if the bus echoes
       (Waveshare 1‑wire bus does this naturally; some chips don't)
    7. read response bytes until <respLen> received or response timeout
    8. return to caller
```

Whether a separate direction‑enable pin is required depends on the
hardware that converts the slot's RX/TX pair to the single‑wire bus.
Three wiring possibilities exist:

| Wiring                                                          | Direction control |
|-----------------------------------------------------------------|-------------------|
| ESP32 RX shorted to ESP32 TX externally (no transceiver).       | Set `txPin` to open‑drain or float when receiving; ESP32 reads its own line. Software must discard the TX echo. |
| Discrete transceiver (e.g. RS‑485 / open‑drain driver) with a DE pin. | DE pin must be raised before TX, lowered after `uart_wait_tx_done`. |
| The mux’s analog switch is itself the bidirectional element.    | Same as the first row — ESP32 is the only driver, and reads its own TX. |

**Open question** (was §9; still open): which of these three applies to
Axiom009. The design accommodates all three by allowing the
`slotControl.slots[i]` JSON to optionally include `dirEnPin` and
`dirEnTxLevel` for the second row, and by always running the cycle in
step 6 above so that the first/third rows work without special config.

### 5.2 Notes

- Steps are sequenced to **never** energise both signalling regimes
  together. In particular the mux's data must be off before the analog
  switch flips to serial, otherwise the UART would see undefined I2C
  edges.
- UART exhaustion is a first‑class error: the API returns
  `failNoUartAvailable` and the slot stays in its previous mode.
- All transitions are idempotent.
- Switching between `SerialFull` and `SerialHalf` does **not** disturb
  the mux state or `serEnPin` — only the UART channel and its adapter
  are recreated.

## 6. Configuration plan

The `SlotControl` block in `SysTypes.json` is currently attached to
`DevMan/Devices`. The proposed migration is to **move the slots array under
the relevant I2C bus** in `HWDevMan/Buses/buslist`, e.g.:

```json
{
    "type": "I2C",
    "name": "I2CA",
    "sdaPin": 7,
    "sclPin": 6,
    "slotControl": {
        "type": "AxiomSlotsV1",
        "slots": [
            { "serEnPin": 108, "serEnLevel": 0, "rxPin": 12, "txPin": 13, "defaultMode": "i2c" },
            ...
        ]
    }
}
```

Reasoning: the slots logically belong to the bus that owns the multiplexer.
This also makes `BusI2C::setup()` the natural place to construct a
`SlotController` and pass it the bus's `BusMultiplexers` and the global
`raftBusSystem` for virtual‑pin operations.

If we want to avoid a SysTypes.json migration in the first iteration, an
acceptable stop‑gap is to keep the old top‑level `SlotControl` block and
have `DeviceManager` look it up by name and forward it to the bus during
post‑setup wiring. This is recorded as an option, not the preferred path.

### 6.1 UART pool management

ESP‑IDF does **not** provide a central "give me any free UART" allocator.
What it does provide (verified against IDF v5.5.3, the version this project
uses) is:

- `SOC_UART_NUM` / `SOC_UART_HP_NUM` (`soc/soc_caps.h`) — total number of
  high‑power UART controllers on the chip, known at compile time. ESP32‑S3
  is `3` (UART0, UART1, UART2). Other chips: ESP32 `3`, ESP32‑S2 `2`,
  ESP32‑C3 `2`, ESP32‑C6 `3`, ESP32‑P4 `5`, etc. Some chips also expose a
  separate `SOC_UART_LP_NUM` (low‑power UART) which we deliberately
  ignore — its driver model is different and it is not appropriate for
  general slot use.
- `uart_is_driver_installed(uart_port_t)` (`driver/uart.h`) — returns
  `true` if some other component has already called
  `uart_driver_install()` on that port. This is the closest thing to a
  global registry and is the right primitive for "is this UART already
  taken?".
- `CONFIG_ESP_CONSOLE_UART_NUM` (Kconfig) — the UART used by the console
  / `printf` / monitor logging. **Must never** be returned by the pool.
  On Axiom009 this is `0`, but the pool reads the Kconfig value rather
  than hard‑coding it so the design works for any board.
- `uart_driver_delete(uart_port_t)` — releases a UART back to ESP‑IDF.
  This is what the pool calls when freeing a slot.

### Algorithm

```
SerialUartPool::allocate() -> std::optional<uart_port_t>
    for n in 0 .. SOC_UART_HP_NUM-1:
        if n == CONFIG_ESP_CONSOLE_UART_NUM:        continue   // reserved
        if n is in our internally-tracked "taken" set: continue
        if uart_is_driver_installed(n):              continue   // someone else owns it
        mark n as taken (in our set)
        return n
    return nullopt   // exhausted

SerialUartPool::release(n)
    uart_driver_delete(n)   // BusSerial owns the install/delete pair, but
                            // pool tracks ownership for safety
    remove n from "taken" set
```

Notes:

- The pool tracks its own "taken" set in addition to consulting
  `uart_is_driver_installed`, because the install happens inside
  `BusSerial::serialInit()` after `allocate()` returns — between the
  allocate call and the install call, another concurrent allocate must
  not pick the same UART.
- The pool is created lazily on first use and protected by a mutex.
- On allocation failure, REST returns `failNoUartAvailable` and the
  slot mode change is rolled back (re‑enable the mux channel, drive
  `serEnPin` back to I2C level).
- **Soft cap of co‑existing serial slots = `SOC_UART_HP_NUM - 1 - (other
  IDF UART users)`**. On Axiom009 (ESP32‑S3, UART0 = console, no other
  UART users currently): up to **2** slots can be in serial mode
  simultaneously. This number is reported via
  `devman/slot/list?bus=I2CA` so a UI can grey out unavailable
  transitions.
- If a future board adds an MCU‑to‑MCU UART used by another component,
  no code change to the pool is needed: that component's
  `uart_driver_install()` call will cause `uart_is_driver_installed()`
  to return `true` and the pool will simply skip it.

### 6.2 Choice of ESP‑IDF UART driver

The project targets ESP‑IDF 5.5.x and 6.0 only. There is sometimes
confusion about whether a "new" UART driver (analogous to the
handle‑based `i2c_master_*` API in `esp_driver_i2c`) should be adopted.
The situation as verified against IDF v5.5.3:

- `components/esp_driver_uart/include/driver/` contains only `uart.h`,
  `uart_select.h`, `uart_vfs.h`, `uart_wakeup.h`, plus `uhci.h` /
  `uhci_types.h`. No handle‑based `uart_new_bus` / `uart_master_*` API
  has been introduced.
- `uart.h` itself is the long‑standing port‑number API
  (`uart_driver_install`, `uart_param_config`, `uart_set_pin`,
  `uart_write_bytes`, `uart_read_bytes`, `uart_wait_tx_done`,
  `uart_flush_input`, `uart_is_driver_installed`). It has been
  modernised internally in 5.x (clock‑source / power‑management
  hooks), but the surface API is unchanged.
- `driver/uhci.h` is a **DMA controller** that sits *on top of* the UART
  driver to offload bulk transfers via GDMA. It is not a replacement
  for `uart.h`. For Waveshare‑class traffic (~1 Mbaud, short packets)
  it adds complexity without measurable benefit.
- IDF 6.0 (in development at the time of writing) does not announce a
  new UART driver replacing `uart.h`.

**Decision: stay on `driver/uart.h`.** Concretely:

- The pool above uses `uart_driver_install` / `uart_driver_delete` /
  `uart_is_driver_installed`.
- `BusSerial` continues to use the same API — no migration is needed
  in this design.
- The half‑duplex cycle in §5.1 maps directly: `uart_flush_input` (step 1),
  `uart_write_bytes` (step 3), `uart_wait_tx_done` (step 4),
  `uart_read_bytes` (step 7).
- If/when a handle‑based UART driver does land in a future IDF, the
  only file that needs to change is `HalfDuplexSerialChannel`
  (and the pool's "is taken?" probe). `BusSerial` already isolates the
  rest of the system from this choice.

## 7. Serial device identification (deferred phase)

The user's intent is to mirror the I2C `DeviceTypeRecord` mechanism
(see [`DeviceTypeRecords.json`](../../../RaftCore/devtypes/DeviceTypeRecords.json)
and the [DeviceTypeRecordFormat wiki](https://github.com/robdobsn/RaftCore/wiki/DeviceTypeRecordFormat))
for serial devices, with at least Waveshare serial servos
([protocol manual](https://files.waveshare.com/upload/2/27/Communication_Protocol_User_Manual-EN%28191218-0923%29.pdf))
as a first target.

The existing `DeviceTypeRecord` is **I2C‑shaped**: `addresses`,
`detectionValues`, `initValues`, `pollInfo` are encoded as I2C
read/write/match strings. For serial devices we need:

| Field (proposed)     | Purpose |
|----------------------|---------|
| `transport`          | `"serial"` to distinguish from I2C records. |
| `baudRate`           | Default baud (Waveshare servos: 1 Mbps). |
| `framing`            | `"halfDuplex"` or `"fullDuplex"` — must be compatible with the slot's mode (`SerialHalf` / `SerialFull`). Mismatch is a setup error. |
| `addressing`         | `"id"` for protocols where each device has a 1‑byte ID; `"none"` for single‑drop. |
| `detection.probe`    | Hex bytes to send to elicit an identifying reply. |
| `detection.match`    | Regex / mask over the reply (similar to I2C `detectionValues`). |
| `pollInfo.command`   | Hex of the poll request. |
| `pollInfo.respLen`   | Expected reply length (or framing rule). |
| `pollInfo.intervalMs`| Poll period. |
| `decoder`            | Symbolic decoder name resolved to a function pointer (same indirection model as `pollResultDecodeFn`). |

Implementation will most likely add a parallel `SerialDeviceTypeRecord`
class plus a separate JSON file (e.g. `SerialDeviceTypeRecords.json`)
rather than overloading the existing record. The two registries can share
the dynamic‑type infrastructure already in `DeviceTypeRecords`.

> This whole section is **deferred**: it should not block the first
> implementation milestone. The first milestone only needs raw bytes
> in/out via the new REST endpoint to be useful.

## 8. Phased implementation plan

Each phase is independently shippable; later phases depend on earlier ones.

### Phase 1 — Mode control plumbing (no UART driver yet)

1. Introduce `SlotController` in `RaftI2C/BusI2C` with:
   - `setup(const RaftJsonIF& slotsCfg, RaftBus* pBus)` reading `slots[]`.
  - `enum class SlotMode { I2C, SerialFull, SerialHalf }`.
  - `setMode(uint32_t slotNum, SlotMode m)` performing the §5 sequence
     **except** UART allocation — in Phase 1 both `SerialFull` and
     `SerialHalf` stop after `virtualPinsSet`, recording that the slot is
     in serial mode and **no longer receiving I2C**. The `BusSerial` /
     half‑duplex adapter is not yet created. The two serial sub‑modes
     are distinguished only by the recorded enum value at this stage.
  - `getMode(uint32_t slotNum)`.
  - `applyDefaults()` — reads each slot's `defaultMode` string and
     calls `setMode(...)` with the corresponding `SlotMode` value
     (`"i2c"`→`I2C`, `"full"`→`SerialFull`, `"half"`→`SerialHalf`).
     Missing field defaults to `I2C`. The legacy boolean `serDefault`
     is accepted as a fallback (see §2).
2. Wire a `SlotController` instance into `BusI2C::setup()` (config under the
   bus, see §6). Provide a `BusI2C::getSlotController()` accessor.
3. Add `devman/slot?bus=&slot=&mode=` to `DeviceManager`. The accepted
   `mode` values are `i2c`, `serial-full`, and `serial-half`. The legacy
   value `serial` is accepted as a synonym for `serial-full`.
   Internally it resolves the bus, calls `getSlotController()`, applies
   the change.
4. Decommission `DeviceSlotControl` (delete the component, its
   registration in `main.cpp`, and the now‑obsolete `slotcontrol/setrate`
   branch — see §4.3).

After Phase 1: I2C scanning and polling correctly skip slots that have been
switched to serial mode, and the user can drive a slot's mode via REST.
There is still no actual serial communication.

#### Phase 1 — testing activity

The ability to test Phase 1 standalone (i.e. before any UART code lands) is
important because all of Phase 1's behaviour is observable as side‑effects
on I2C scanning and on the `serEnPin` line. The testing plan is:

1. **Hardware verification of `serEnPin` toggling.**
   - Attach a logic analyser or scope to each slot's `serEnPin` (or the
     I/O‑expander pin it drives) and to the slot's mux SDA/SCL.
   - Boot with `defaultMode="i2c"` (or absent) for all slots; verify
     `serEnPin` is at `!serEnLevel` and that I2C devices on each slot
     are scanned and polled normally (use `devman` JSON output or the
     existing TestWebUI).
   - Issue `devman/slot?bus=I2CA&slot=2&mode=serial-full`. Verify on the
     analyser that:
     a. The mux's channel‑enable bit for slot 2 goes low **first**.
     b. Then `serEnPin[1]` (slot 2) transitions to `serEnLevel`.
     c. No further I2C clocks appear on slot 2.
   - Issue `devman/slot?bus=I2CA&slot=2&mode=serial-half`. Verify that
     `serEnPin[1]` and the mux state are **unchanged** (this is a
     UART‑layer transition only — visible on the analyser only as a
     subsequent change in TX driving behaviour once Phase 2 lands).
   - Issue `devman/slot?bus=I2CA&slot=2&mode=i2c`. Verify the reverse
     ordering: `serEnPin[1]` flips back, **then** the mux re‑enables the
     channel. Verify that the device on slot 2 reappears in the next scan.
   - Reboot with `defaultMode="full"` on slot 3 only; verify slot 3 is
     in `SerialFull` at boot (no scan traffic on it; `serEnPin[2]` at
     `serEnLevel`) and that the other four slots scan as normal. Repeat
     with `defaultMode="half"` on slot 3 and verify the recorded mode is
     `SerialHalf` (analyser side‑effects identical at this phase).
   - Boot once more with the legacy `serDefault: 1` (and no
     `defaultMode`) on slot 3; verify it is interpreted as
     `SerialFull` and a deprecation warning is logged.

2. **Software verification (no hardware needed).**
   - Add a small unit test under `raftdevlibs/RaftI2C/unit_tests/` that
     constructs a `SlotController` with a stub `RaftBus` and a stub
     virtual‑pin sink, then exercises:
     - `applyDefaults()` correctly maps each `defaultMode` value
       (`"i2c"`, `"full"`, `"half"`, missing, unknown) to the right
       `SlotMode`, and the legacy `serDefault` boolean falls back as
       documented.
     - `setMode(SerialFull)` and `setMode(SerialHalf)` both call
       `enableSlot(n, true, false)` **before**
       calling `virtualPinsSet(serEnPin, serEnLevel)`. Order is checked
       by recording calls into a vector on the stubs.
     - `setMode(I2C)` reverses the order.
     - `SerialFull ↔ SerialHalf` transitions do **not** call
       `enableSlot` or `virtualPinsSet` (they are UART‑only).
     - Out‑of‑range `slotNum` returns `RAFT_INVALID_DATA`.
     - Idempotent re‑application of the current mode is a no‑op (no
       mux write, no virtual pin write).
   - Add a REST‑level test in `TestWebUI` (or via curl against a flashed
     device) that issues each `devman/slot?...` command and checks the
     JSON response.

3. **Regression tests.**
   - Confirm that with all slots in I2C mode (the default for Axiom009)
     existing scans, polls and `devman/cmdraw` behaviour are unchanged.
   - Confirm that `devman/devconfig` rate control still functions on
     slots that remain in I2C mode (this is what replaces the deleted
     `slotcontrol/setrate`).

4. **Documentation check.**
   - Update [`README.md`](../README.md) and any TestWebUI docs to mention
     the new `devman/slot?...` endpoint and to note that `slotcontrol/...`
     has been removed.

### Phase 2 — Serial channel allocation

1. Add `SerialUartPool` driven from ESP‑IDF SoC capabilities at runtime
   (see §6.1 below). It is **not** a hard‑coded list per chip — it derives
   the candidate set from `SOC_UART_HP_NUM`, excludes the console UART
   from `CONFIG_ESP_CONSOLE_UART_NUM`, and queries
   `uart_is_driver_installed()` to skip UARTs already in use by other
   components.
2. Extend `SlotController::setMode(SerialFull|SerialHalf)` to allocate a
   UART and instantiate a `BusSerial` configured from the slot's `rxPin`,
   `txPin`, and a `baudRate` taken first from REST query, then from the
   bus config default, then a built‑in default (e.g. 1 000 000 for
   Waveshare). For `SerialHalf`, additionally wrap the `BusSerial` in a
   `HalfDuplexSerialChannel` adapter that implements the request/response
   cycle from §5.1 (flush‑RX, write, `uart_wait_tx_done`, optional DE
   pin toggle, echo‑discard, read response).
3. Register the channel with `raftBusSystem` under a derived name
   (e.g. `"<busName>.s<n>"`) so it appears in `devman/busname` and can be
   addressed by the existing REST machinery. Both sub‑modes register
   under the same name — the duplex setting is an attribute of the
   channel, not part of its identity.
4. Add `devman/slot/cmd?bus=&slot=&hexWr=&numToRd=` for raw byte I/O.
   Behaviour by mode:
   - `SerialFull`: fire‑and‑forget write plus a non‑blocking RX read after
     a configurable timeout (mirrors `devman/cmdraw` for I2C).
   - `SerialHalf`: synchronous request/response — driver runs the
     §5.1 cycle and returns when `numToRd` bytes are received or
     timeout expires.
5. Surface UART exhaustion as `RAFT_INSUFFICIENT_RESOURCE` /
   `failNoUartAvailable` and ensure `setMode(SerialFull|SerialHalf)`
   rolls back the slot to its prior mode cleanly when allocation fails.

After Phase 2: the user can talk to any serial peripheral attached to a
slot from the REST API, manually framing requests and responses.

### Phase 3 — Typed serial devices (Waveshare servo first)

1. Define `SerialDeviceTypeRecord` (see §7) and a JSON registry file.
2. Implement a `SerialDeviceIdentMgr` analogous to
   [`DeviceIdentMgr`](../components/RaftI2C/BusI2C/DeviceIdentMgr.h) for I2C,
   but only when explicitly triggered (we will not auto‑probe a serial
   slot — we only probe on user/configuration request, since serial probes
   are not as cheap or safe as I2C ones).
3. Add a Waveshare servo driver as the first concrete `SerialDevice` and
   integrate it with the existing device polling/data‑capture pipeline so
   that polled values appear in `devman` JSON / binary streams the same
   way I2C device values do.
4. Extend `devman/slot?...&mode=serial&type=<deviceTypeName>` to bind a
   typed driver to the slot at mode‑switch time.

## 9. Open questions / things to confirm before coding

- **Half‑duplex direction control wiring.** See §5.1: which of the three
  electrical wirings (self‑echoing, discrete transceiver with DE pin,
  analog‑switch bidirectional) does Axiom009 actually use? The
  `dirEnPin` / `dirEnTxLevel` JSON fields are reserved for this; if no
  DE pin is needed they remain absent.
- **UART pool size.** UART0 is the console; UART1 and UART2 are normally
  available on ESP32‑S3. With five physical slots, only **two** can be
  in any serial mode (full or half) at a time on this chip. This must be
  documented as a hard limit and reported via `devman/slot/list`.
- **SysTypes.json layout.** Whether to migrate `SlotControl` under
  `HWDevMan/Buses/buslist[I2CA]/slotControl` (preferred) or keep it under
  `DevMan/Devices` for the first iteration.
- **Persistence.** Should slot mode (including serial sub‑mode) survive
  a reboot, overriding `defaultMode`? Most likely yes (NVS), but out of
  scope for Phase 1.

## 10. Summary of changes by file (no edits made yet)

| File                                                                                              | Change |
|---------------------------------------------------------------------------------------------------|--------|
| `raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/SlotController.{h,cpp}`                            | **New.** Slot mode state machine. |
| `raftdevlibs/RaftI2C/components/RaftI2C/BusI2C/BusI2C.{h,cpp}`                                    | Construct/own a `SlotController`; expose accessor. |
| `raftdevlibs/RaftI2C/components/RaftI2C/SerialSlots/SerialUartPool.{h,cpp}`                       | **New.** UART number allocator. |
| `raftdevlibs/RaftI2C/components/RaftI2C/SerialSlots/SerialSlotBus.{h,cpp}`                        | **New.** Per‑slot `BusSerial` + `HalfDuplexSerialChannel` lifetime management; also routes raw byte I/O for both serial sub‑modes. |
| `raftdevlibs/RaftCore/components/core/DeviceManager/DeviceManager.{h,cpp}`                        | Add `devman/slot...` sub‑commands. |
| `raftdevlibs/RaftCore/components/core/DeviceTypes/SerialDeviceTypeRecord.{h}` *(Phase 3)*         | **New.** Schema for serial devices. |
| `raftdevlibs/RaftCore/devtypes/SerialDeviceTypeRecords.json` *(Phase 3)*                          | **New.** Registry — initial entry: Waveshare servo. |
| `components/DeviceSlotControl/` (in this repo)                                                    | **Delete** after Phase 1 completes. |
| `main/main.cpp`                                                                                   | Remove `DeviceSlotControl` registration; nothing else changes (no need to register `BusSerial` directly — `SerialSlotBus` does that internally). |
| `systypes/Axiom009/SysTypes.json`                                                                 | (Optional, see §6) move `SlotControl` under the I2C bus. |

## 11. Risk / non‑goals

- **No silent data loss.** Switching a slot to serial must atomically
  remove it from I2C scanning before flipping the analog switch — the
  state machine in §5 enforces this.
- **No surprise UART grabs.** The UART pool must be explicit; the
  console UART is never returned by the pool.
- **No automatic probing of serial slots.** Unlike I2C, an unknown
  serial peripheral could be damaged by random probes. Phase 3
  identification is opt‑in only.
- **No change to the I2C scanner internals.** The existing
  `disabledSlotsMask` mechanism already does what we need; we are not
  adding a new code path inside `BusScanner` / `BusMultiplexers`.

---

*Prepared as a pre‑implementation design — no source files have been
modified.*
