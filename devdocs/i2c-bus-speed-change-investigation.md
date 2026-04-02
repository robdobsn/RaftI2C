# I2C Bus Speed Change Investigation

Date: 2026-04-02

## Context

Two commits to RaftI2C were investigated:

1. **52ed6e73** — Adds per-device bus frequency override. The bus speed is changed to 400KHz for specific devices (e.g., LSM6DS3) and restored after the transaction. Adds `_appliedBusFrequency` cache and short-circuit in `setBusFrequency()`.

2. **e2d9b892** — Replaces the `vTaskDelay(0)` spin-wait with a FreeRTOS binary semaphore for waiting on I2C transaction completion. ISR gives the semaphore via `xSemaphoreGiveFromISR()`.

## Observed behaviour

| Config | avgUs | maxUs | Notes |
|--------|-------|-------|-------|
| Neither commit | 1716 | 22063 | Baseline |
| Commit 1 only | 3722 | 61449 | 2.2× worse avgUs |
| Both commits | 931 | 7457 | Best performance |

With both commits, read transactions from the bus seem to terminate early at higher sample rates.

## Attempted fixes (REVERTED — did not solve the problem)

### Fix 1: Disable interrupts on software timeout

In the `access()` function, when software timeout fires and `!allCmdsDone`, interrupts were left enabled. A late ISR could give the semaphore after timeout handling, leaving a stale token for the next transaction.

**Change:** Added `I2C_DEVICE.int_ena.val = 0` and `I2C_DEVICE.int_clr.val = _interruptClearFlags` in the `else` branch (~line 462).

**Rationale:** Prevent late ISR from giving stale semaphore token after software timeout.

### Fix 2: Drain stale semaphore before new transaction

Added `xSemaphoreTake(_accessSemaphore, 0)` (non-blocking drain) after clearing/enabling interrupts but before resetting `_accessResultCode` and starting `trans_start`.

**Rationale:** Belt-and-suspenders catch for any stale semaphore token from a previous transaction's late ISR.

### Fix 3: Handle simultaneous ACK_ERR + TRANS_COMPLETE in ISR

The old `else if` chain meant that if both `ACK_ERR` and `TRANS_COMPLETE` status bits were set when the ISR ran, ACK_ERR processing would skip TRANS_COMPLETE entirely. The semaphore would never be given; the task would wait until software timeout.

**Change:** Restructured the ISR so that within the `else` branch (after checking TIMEOUT and ARB_LOST), both `ACK_ERR` and `TRANS_COMPLETE` are checked independently using separate `if` statements rather than `else if`.

**Rationale:** At 400KHz, events happen 4× faster, making simultaneous interrupt status bits more likely. Missing TRANS_COMPLETE would cause 8ms+ software timeout instead of prompt completion.

### Fix 4: Invalidate `_appliedBusFrequency` in `reinitI2CModule()`

After `fsm_rst`, `setBusFrequency(_busFrequency)` was short-circuited by the cache check. The clock registers might not be reprogrammed after a hardware reset.

**Change:** Added `_appliedBusFrequency = 0` before `setBusFrequency(_busFrequency)` in `reinitI2CModule()`.

**Rationale:** Force clock register writes after FSM reset.

## Why these fixes were reverted

These fixes did not resolve the observed early read termination problem. Further investigation with DEBUG_TIMEOUT_CALCS revealed a different issue — see "Overhead calculation analysis" below.

## Overhead calculation analysis (from DEBUG_TIMEOUT_CALCS data)

The per-byte overhead constant `CLOCK_STRETCH_AND_SCHED_OVERHEAD_PER_BYTE_US = 500` creates absurdly large timeout values for large transactions at 400KHz:

| Transaction bytes | Wire time @400KHz | Overhead | maxExpectedUs | Actual time |
|-------------------|-------------------|----------|---------------|-------------|
| 3 | 75µs | 2000µs | 2300µs | ~75µs |
| 7 | 175µs | 4000µs | 4175µs | ~175µs |
| 387 | 9675µs | 194000µs | 203675µs | ~10ms |

For the 387-byte FIFO read, the timeout allows 203ms when the actual wire time is ~10ms. While this doesn't cause the early-termination bug per se (it's an upper bound, not a delay), it means that if the hardware *does* stall, recovery takes 200ms+.

At 208Hz sampling, the LSM6DS3 FIFO accumulates rapidly. The repeating pattern every ~210ms shows:
- Small transactions (3, 2 bytes): I2C address/status probes for other devices
- Medium transaction (7 bytes): Register read
- Large transaction (63-387 bytes): FIFO bulk read — grows over time and eventually stabilises at 387 bytes

The FIFO byte count keeps growing (63 → 87 → 99 → 147 → 183 → 243 → 375 → 387 → 387...) until it saturates at the FIFO watermark/capacity limit. This suggests the polling interval is not keeping up with the 208Hz data rate initially, then stabilises once the FIFO fills and is read at a fixed cadence.

## ROOT CAUSE FOUND: ESP32-S3 I2C read size limit (2026-04-02)

### The problem

The ESP32-S3 I2C hardware has:
- **8 command queue slots** (`I2C_ENGINE_CMD_QUEUE_SIZE = 8`)
- Each READ command can transfer up to **255 bytes** (`I2C_ENGINE_CMD_MAX_RX_BYTES = 255`)
- RX FIFO is **32 bytes** deep — drained by the ISR via RXFIFO_FULL interrupts during the transaction

For a write-restart-read (FIFO read pattern), the command slots are:
1. RSTART
2. WRITE (address + register)
3. RSTART
4. WRITE (address + read bit)
5. READ (up to 255 bytes, ACK)
6. READ (1 byte, NACK — final byte)
7. STOP

= 7 commands for ≤256 byte reads. This fits in 8 slots.

For reads >255 bytes (e.g., 384), additional READ commands are needed. 384 bytes requires READ(255) + READ(128) + READ(1, NACK) = 3 READ commands, totalling 9 commands — which barely fits in the 8 slots (the code's existing check passes it). However, at 400KHz the ISR must drain the 32-byte RX FIFO every 800µs. With WiFi/BLE interrupts potentially delaying the ISR, FIFO overflow occurs at 400KHz but not at 100KHz (where the FIFO fills every 3.2ms).

### Evidence from logic analyser

With both commits (400KHz, 208Hz sampling):
- FIFO status register 0x3A read returns `B4 8F 00 00` → 384 bytes available (32 × 12-byte samples)
- 384 bytes read from register 0x3E
- Next read of 0x3A returns `00 E0 02 00` → error/overrun flag set
- All subsequent reads from 0x3A return the same error values — FIFO is stuck

This is consistent with: the multi-command read (384 bytes, 3 READ commands at 400KHz) causes RX FIFO overflow in the ESP32 I2C peripheral. The LSM6DS3 delivers data correctly on the wire, but the ESP32 loses bytes, returns corrupted results, and the device gets into a bad state.

### Fix: Clamp reads to 255 bytes

Added a clamp in `RaftI2CCentral::access()` to limit `numToRead` to `I2C_ENGINE_CMD_MAX_RX_BYTES` (255):

```cpp
if (numToRead > I2C_ENGINE_CMD_MAX_RX_BYTES)
{
    LOG_W(MODULE_PREFIX, "access CLAMPED read from %d to %d addr %02x",
          (int)numToRead, (int)I2C_ENGINE_CMD_MAX_RX_BYTES, (unsigned)address);
    numToRead = I2C_ENGINE_CMD_MAX_RX_BYTES;
}
```

### Verified behaviour with fix

- 208Hz and 416Hz sampling rates work continuously without stopping
- `avgUs` ≈ 440-1040µs (vs 3722µs with commit 1 only, or 1716µs baseline without either commit)
- `maxUs` ≈ 3300-15000µs (vs 61449µs with commit 1 only)
- Reads are clamped from 288-384 down to 255 — unread FIFO data is picked up on the next poll cycle
- The FIFO does not overflow or get stuck because data is drained incrementally

### Remaining consideration

The 255-byte clamp means the upper layer reads fewer bytes than the FIFO contains. The remaining data is read on the next poll cycle. At very high sample rates this may cause gradual FIFO accumulation, but since the LSM6DS3 FIFO can hold up to 4096 bytes, and each poll reads 255 bytes (21 samples), this is stable as long as the poll interval doesn't fall too far behind the data production rate.

The proper longer-term fix would be to split large reads into multiple separate I2C transactions (each ≤255 bytes) rather than trying to fit them into a single multi-command transaction. This would avoid both the command queue limit and the FIFO drain timing pressure.
