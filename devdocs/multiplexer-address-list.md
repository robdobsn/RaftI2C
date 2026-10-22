# Multiplexer addresses as a list: proposal

Date: 2026-10-18.

**Status:** proposed, not started.

## 1. Why

Bus multiplexers are configured as a contiguous address range: `"mux": {"minAddr": "0x75", "maxAddr": "0x76"}`.

**How the range is used:**
- A multiplexer's slot numbers come from its offset in the range: offset 0 gives slots 1-8, offset 1 gives 9-16.
- Every address in the range is treated as a multiplexer. It is only scanned on the multiplexer's own connection (normally the main bus), so **a device at that address on a slot is never seen**.

**Why that is a problem:**
- Adding a second multiplexer means reserving every address between the two.
- The obvious next address after 0x75 is 0x76, and the end of the range is 0x77. Both are among the most useful sensor addresses:

| Address | Users |
|-|-|
| 0x77 | Default on Adafruit and SparkFun boards for BME280, BME680, BMP280, BMP388, BMP390 and DPS310. Moving one needs a solder jumper. Also MS5607/MS5611, and the old BMP180/BMP085 (0x77 only) |
| 0x76 | The only address of the MS8607 pressure/temperature part, the MS5637 and the MS5837 (Blue Robotics depth sensor). Also the other address of the dual-address Bosch sensors |
| 0x74 | IS31FL3731 default; HT16K33, PCT2075, TCA9548 (selectable) |
| 0x71-0x72 | HT16K33, PCT2075, TCA9548 only - all selectable. The quietest addresses in the TCA9548's 0x70-0x77 |
| 0x70 | SHTC3 (fixed), HT16K33 default, PCA9685 all-call |

(Adafruit's I2C address list, `0x70-0x7F.md`, plus the MS5637 and MS5837.)

Axiom018 has one multiplexer, at 0x75, and reserved 0x76 for a second. On 2026-10-18 its range was narrowed to 0x75 alone, freeing 0x76.

A second multiplexer is best placed at 0x72 or 0x71. That needs **a list of multiplexer addresses rather than a range**, so 0x76 and 0x77 stay free.

## 2. What to do

**Configuration:** `"mux": {"addrs": ["0x75", "0x72"], ...}`.
- A multiplexer's index, and so its slot numbers, is its position in the list. 0x75 keeps slots 1-8 and 0x72 gets 9-16, so adding one doesn't renumber the existing slots.
- `minAddr`/`maxAddr` are still accepted and expanded to a list (the default stays 0x70-0x77, as now).
- The limit stays at 8 multiplexers.

**`BusMultiplexers`** (`BusMultiplexers.h/.cpp`):
- Hold the list (`_muxAddrs`).
- Replace the `_minAddr + index` and `addr - _minAddr` arithmetic with lookups, in:
  - `isBusMultiplexer`;
  - `isSlotCorrect`;
  - `getActiveMuxAddrs`;
  - `getAddrFromMuxIdx`;
  - the setup and reset loops;
  - the record lookups (`_busMuxRecs[addr - _minAddr]`).
- Add `getMuxIdx(addr)` (returns -1 if the address isn't a multiplexer).
- Replace `getMinAddr`/`getMaxAddr` with `getMuxAddrs()` (keep the old accessors only if something outside RaftI2C uses them).

**`BusScanner`:**
- `isAddrClaimable`: use `isBusMultiplexer`.
- The multiplexer presence check (`_muxCheckIdx` over `maxAddr - minAddr + 1`): iterate the list.
- When a multiplexer comes or goes: `muxIdx = getMuxIdx(addr)`, not `addr - getMinAddr()`.
- `SCAN_MODE_MAIN_BUS_MUX_ONLY`: index into the list.

**Elsewhere:**
- Check the I/O expander config (`muxAddr`), the power controller and the slot controller for uses of the range.
- Check SysTypes that rely on the default range.
- Update the wiki's bus configuration page.

## 3. Testing

- **Host tests** for the address/index mapping:
  - a list in any order;
  - a range expanded to a list;
  - a non-multiplexer address;
  - slot numbers for each index.
- **Axiom018 with `"addrs": ["0x75"]`:** the same slots, identification and devices as with `minAddr`/`maxAddr` 0x75; a device at 0x76 on a slot is seen.
- **With a second TCA9548 at 0x72** (main bus, or on a slot of the first): its slots appear as 9-16 and devices on them are identified; hot-plugging either multiplexer works; 0x76 and 0x77 devices are still seen on every slot.
