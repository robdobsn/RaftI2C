# I²C Poll Data Integrity — CRC + Re-read Plan

> Status: **plan / design** — 2026-08-22. Target: the **VCP** (`RoboticalVCP`, DTID `0x0111`)
> first, since that hardware is on the bench; the mechanism is designed to generalise to every
> polled RSAO (sound sensor next).
>
> Related: [i2c-adaptive-yield-plan.md](i2c-adaptive-yield-plan.md),
> [i2c-bus-speed-change-investigation.md](i2c-bus-speed-change-investigation.md),
> MultiFirmware `devdocs/i2c-bootloader-protocol-spec.md`, `docs/app-vcp.md`.

---

## 0. Problem

On an I²C **master read** the master drives ACK, so nothing in the protocol detects
slave→master corruption. Every byte of VCP measurement data currently arrives unverified. A
glitch on **SCL** is the dominant failure mode: an extra or swallowed clock edge desynchronises
the slave's shift register and the remainder of the burst is garbage — silently decoded into
V/V2/V3/I readings.

Existing protection covers only the write path, and only partly:

| Path | Protection today |
|---|---|
| Framed bootloader/arbitration commands | 16-bit `~(Σ−1)` checksum |
| Unframed app writes (`0x30`–`0x39`, `0xFF 0x40 …`) | **none** (slave ACKs regardless) |
| **All reads** (FIFO, STATUS, CONFIG, identity) | **none** |

The physical-layer mitigation is already in: `I2C_DIGITAL_NOISE_FILTER = 4` (500 ns at the
8 MHz HSI kernel clock), committed in MultiFirmware `4fe7837`. That lowers the *rate* of
corruption at the slave's receiver. It does nothing for corruption at the master's receiver,
and nothing detects what gets through. This plan adds end-to-end detection plus recovery.

---

## 1. Current state (measured 2026-08-22, not assumed)

**Slave — VCP app** (`apps/vcp/`)

| Item | Value | Source |
|---|---|---|
| `RSAO_I2C_COMMS_TX_MAX` | **132** (VCP override; 80 default) | `apps/vcp/appConfig.h:42` |
| `RSAO_I2C_COMMS_RX_MAX` | 80 | `sysConfigPlatform.h:125` |
| FIFO capacity | 512 B | `appConfig.h:47` |
| Scan burst | 4 B header + **16** × 8 B = **132 B exactly** | `VcpApp::buildFifoData` |
| Fast burst | 4 B header + **64** × 2 B = **132 B exactly** | ditto |
| Header | `[n][mode][channel][ovfCount]`, `ovfCount` now via `popOvfCount()` (clears on read) | `SampleFifo.h:111` |
| App flash | 10684 / 23552 (45.4 %) | build |
| App RAM | 1852 / 3904 (47.4 %) | build |

**Slave — bootloader**

| Item | Value |
|---|---|
| Flash | **6080 / 6144 — 64 bytes free** (PlatformIO reports "5872"; that figure omits `.isr_vector` and the init/fini arrays and overstates the headroom — see the correction note in §2) |
| RAM | 840 / 4096 |

**Master** (`RoboticalAxiom1`)

- Poll spec: `"c": "0x21=r132", "i": 25, "s": 38` — `AxiomDevTypes/RSAOTypeRecords.json`.
- Poll execution: `DevicePollingMgr::taskService`, transaction at
  [DevicePollingMgr.cpp:134](../components/RaftI2C/BusI2C/DevicePollingMgr.cpp#L134); result
  stored at line 182. **No retry logic exists anywhere in RaftI2C** (the only `retry` hits are
  bus-stuck recovery).
- `pollInfo` keys parsed today: `c`, `s`, `i`, `h`/`busHz`, slot mask —
  `RaftCore/.../DeviceTypeRecords.cpp:200-360`.
- `raftdevlibs/RaftI2C` is the live copy (git remote `robdobsn/RaftI2C`), byte-identical to
  `/z/home/rob/rdev/raft/RaftI2C`. Edit the `raftdevlibs` copy; version bump + `dependencies.lock`.
- New since the original analysis: `components/RoboticalRSAO/` holds the RSAO protocol core
  (`RSAOProtocol` — pure, host-unit-testable) and **`RSAOTestMode`**, which re-implements the
  QT Py bridge command grammar (`WR`, `AREAD`, `CLK`, `TPOLL`, …) on the Axiom itself. The
  MultiFirmware pytest suite can therefore drive the bench VCP through the Axiom with no QT Py.

---

## 2. Constraints that shape the design

These are the reasons the obvious implementations do not work.

**C1 — The bootloader has 64 bytes of flash free.** `I2CPerif` is shared between the
bootloader and every app. Any CRC code added there lands in the bootloader unless guarded.
A 256-byte table is impossible and even a bitwise CRC-8 (~40 B) would consume most of what
is left. There is precedent: MultiFirmware `devdocs/rsao-address-arbitration-as-built.md`
records that *"a table-less CRC-8 was prototyped but did not fit the bootloader's tight flash
budget"* — which is why the serial-number check byte is an additive checksum, not a CRC.
→ **All CRC code must be `#ifndef BOOTLOADER_APP`.** The bootloader keeps its `~(Σ−1)` framing
checksum and gains nothing from this work.

> ### ⚠ Correction (2026-08-25): the headroom figure used throughout this document
>
> Earlier revisions of this plan said **272 bytes free**, taken from PlatformIO's
> `Flash: [==========] 95.6% (used 5872 bytes from 6144 bytes)`. **That figure is wrong for
> this purpose.** PlatformIO's number counts `.text + .rodata + .data` only; it omits
> `.isr_vector` (192 B) and the `.init_array`/`.fini_array` pair (16 B), which occupy flash
> just the same:
>
> ```
> .isr_vector 192 + .text 5700 + .rodata 64 + .init/fini 16 + .data 108 = 6080 / 6144
> ```
>
> **Real headroom is 64 bytes.** This reconciles the linker arithmetic exactly: the
> command-path latch narrowing (§9) adds 68 bytes → 6080 + 68 = 6148 → *"region FLASH
> overflowed by 4 bytes"*. It was **4 bytes short, not ~276** as previously recorded here.
>
> Consequence: the bootloader is far tighter than this document implied, *and* the changes
> abandoned on flash grounds are far closer to fitting than implied. Use the linker's
> overflow message or the `size -A` section totals — never PlatformIO's Flash line — when
> judging bootloader headroom.
>
> A route to real headroom exists: the bootloader links newlib's heap (`_malloc_r` 188 B,
> `_free_r` 148 B, `impure_data` 96 B, `_raise_r` 84 B ≈ **520 B**) solely because
> `RingBuffer` does `_pBuf = new T[_size]` for `RSAOComms::_commandQueue`. Making that
> compile-time sized should reclaim ~500 bytes. See
> `RoboticalAxiom1/devdocs/rsao-address-assignment-revised-plan.md` step 1.

**C2 — The VCP burst is exactly full.** 4 + 16×8 = 132 and 4 + 64×2 = 132. There is no spare
byte inside the current response. A CRC must extend the response, not fit inside it.

> **Is `RSAO_I2C_COMMS_TX_MAX` a hard stop on the wire?** No — it is a **buffer size, not a
> transfer limit**, so extending the response past 132 bytes is safe. At address match the slave
> stages `toTx = min(txLen, RSAO_I2C_COMMS_TX_MAX)` into `_txBuf[132]`; on the wire the TXIS
> handler sends `(_txBufPos < _txBufLen) ? _txBuf[_txBufPos++] : 0`. There is no terminating
> condition — the master clocks as many bytes as it wants and simply receives `0x00` once the
> staged data runs out. A master reading 135 bytes against today's firmware gets 132 real bytes
> and 3 zeros: no NACK, no error, no truncation. What the 132 *does* limit is how much **payload
> can be staged**, which is why the trailer is generated on the fly in TXIS (§3.2) rather than
> buffered — `TX_MAX` stays at 132 and no RAM is added. The real ceiling is on the master:
> `RaftI2CCentral::MAX_I2C_READ_BYTES = 240`, which silently truncates above that.

**C3 — The response callback runs in the address-match ISR window.** `I2CPerif.cpp` builds the
whole response at `ADDR` match, before any byte is clocked. `RSAOComms.cpp:181` already carries
a warning that overrunning this window causes a NACK on address match. A CRC over 132 bytes
computed there is ~8 µs of work in the worst possible place.

**C4 — FIFO reads are destructive.** `buildFifoData()` calls `_fifo.pop()` at address-match,
*before* the transfer that might corrupt it. A plain re-read returns the *next* burst, not the
failed one. This is why the re-read command is needed rather than just "read again".

**C5 — Retry is not expressible in the poll-request grammar.** `pollInfo.c` describes an
unconditional sequence executed every poll. A conditional re-read must be native code in
`DevicePollingMgr`, driven by new `pollInfo` config.

---

## 3. Design

### 3.1 Generic trailer — the core idea

Every app-layer response gains a **3-byte trailer `[seq][crcHi][crcLo]`** appended after a
**declared response length `L`**. The master reads `L + 3` bytes. The CRC covers bytes
`0 … L-1` (including any zero padding) plus the `seq` byte.

```
   master reads 135                        ┌── CRC covers these 132 bytes ──┐
   ┌───┬──────┬────┬─────┬─────────────────┴───┬──────────────────────────┴─┬─────┬─────┬─────┐
   │ n │ mode │ ch │ ovf │ entry 0 … entry n-1 │ 0x00 padding to L=132      │ seq │ crc │ crc │
   └───┴──────┴────┴─────┴─────────────────────┴────────────────────────────┴─────┴─────┴─────┘
     0    1     2     3    4                                            131   132   133   134
                                                                              └─ CRC also covers seq
```

**`L` is declared by the slave, not inferred from the master's read length.** This is the one
subtlety in the scheme: the slave cannot know how many bytes the master intends to clock, so the
trailer position must be fixed by the *response*, not the *request*. Rule:

> `L` defaults to the length of data the response callback returns. A response whose natural
> length varies declares a constant `L` instead and is zero-padded to it.

For most reads `L` is already constant and nothing changes: STATUS `L=8`, CONFIG `L=16`, WHOAMI
`L=15`, serial `L=8`, version `L=3`, RSAO-check `L=9`, flash read `L=32`. **Only the VCP FIFO
read needs an explicit declaration** (`L = 132`), because its natural length is `4 + n ×
entryBytes` and varies with FIFO occupancy.

Crucially the padding costs nothing: `TXIS` already emits `0x00` past `_txBufLen`, so declaring
`L = 132` needs **no `memset` and no extra buffer** — the zeros are generated on the fly exactly
as they are today, and folded into the running CRC as they go.

Why a declared fixed `L` rather than a CRC at `4 + n × entryBytes`:

- **No offset arithmetic on either side.** The master does not parse `n`/`mode` to find the CRC,
  so no expression evaluator and no per-device offset config. The rule is uniform:
  *"CRC covers the first L bytes; trailer is the next 3."*
- It protects `n` and the rest of the header, not just the payload.
- It generalises unchanged to every polled RSAO: the sound sensor becomes `=r35`, `L=32`.

**Backward compatible.** The master controls read length. A master still reading 132 simply
never clocks the trailer — new firmware works with the old device type record. A master reading
135 against old firmware gets three `0x00` bytes, which fails CRC and is correctly reported as an
error. Roll out firmware first, record second.

### 3.2 Computing the CRC in the TXIS handler (solves C3)

Because the trailer is at the **end**, the CRC can be accumulated byte-by-byte as bytes are
shifted out, in the existing `TXIS` branch (`I2CPerif.cpp:205`) — a few cycles per byte spread
across the whole transfer, and **nothing added to the address-match window**.

State `I2CPerif` gains (guarded by `#ifndef BOOTLOADER_APP`):

```
_txRespLen          // declared response length L (defaults to _txBufLen)
_txSeq              // sequence byte for this response
_txRunningCrc       // updated for every byte shifted out, incl. padding and seq
```

TXIS logic becomes, on each byte:

| `_txBufPos` range | Byte sent | Folded into CRC |
|---|---|---|
| `< _txBufLen` | `_txBuf[pos]` | yes |
| `_txBufLen … L-1` | `0x00` (padding, as today) | yes |
| `L` | `_txSeq` | yes |
| `L+1`, `L+2` | CRC hi, CRC lo | — |
| `> L+2` | `0x00` | — |

The response callback gains an optional out-parameter for `L`; when it is not set, `L` =
the returned data length, so every existing response keeps working unchanged and only
`VcpApp`'s FIFO read declares `L = 132`.

**RAM cost on the STM32: four bytes of state.** `_txBuf` and `_i2cTxBuf` stay at 132 — the
trailer and padding are generated on the fly, never buffered. C2 is satisfied without raising
`TX_MAX`.

**Decision 3 — trailer on all reads: adopted.** There is no reason not to. A master that does
not read the extra bytes never clocks them, so it costs nothing on responses nobody validates,
and it means one mechanism rather than a per-selector exception list. The **one** exclusion is
the bootloader (C1), which keeps bare responses and its `~(Σ−1)` framed-command checksum. Note
the consequence: identity reads used during detection/arbitration are unprotected while the
device is in the bootloader, and `detectionValues` entries read exact byte counts so they are
unaffected either way.

### 3.3 Re-read command (solves C4)

`_i2cTxBuf` still holds the last assembled burst after `pop()`. So:

> **New selector `0x22` REREAD** — re-transmit `_i2cTxBuf` verbatim, with the **same `seq`**,
> without popping the FIFO.

- **Zero extra RAM** — the burst is already there.
- STATUS uses `RSAOComms::_responseBuf` and CONFIG repoints at `_calStaging`, so neither
  clobbers `_i2cTxBuf`; only another FIFO read does, and the master is the only reader.
- The matching `seq` is what lets the master *prove* it received the same burst rather than a
  fresh one.

Master algorithm per poll:

1. Read 135 at `0x21`.
2. Validate CRC over bytes 0–132 (payload + padding + `seq`) against bytes 133–134, plus header
   plausibility (`n ≤ maxFit`, `mode ∈ {1,2}`, `ch ≤ 3`).
3. On failure → read 135 at `0x22`; validate again. Repeat up to `retries` times.
4. Still failing → **drop the burst**, increment the error counter. Do not mark the device
   offline (this is data corruption, not absence).
5. Track `seq` continuity to count bursts lost this way; a repeated `seq` on a normal read
   means a duplicate and should be dropped.

### 3.4 CRC choice — CRC-16-CCITT from the start

**Decision 1: CRC-16-CCITT** (poly 0x1021, init 0xFFFF, big-endian on the wire). The overhead
over CRC-8 is negligible on every axis that matters, and on the master it is *less* work:

| Axis | CRC-8 (0x07) | CRC-16-CCITT | Verdict |
|---|---|---|---|
| Slave cycles/byte, bitwise, M0 @48 MHz | ~30 (≈0.6 µs) | ~40 (≈0.8 µs) | Budget per byte at 100 kHz is **90 µs**. Both use ~1 % |
| Slave flash | ~40 B | ~55 B bitwise, or 32 B nibble table + ~40 B | App has **12.8 kB** free |
| Slave RAM | 1 B | 2 B | Noise |
| Wire | 2 B trailer | 3 B trailer | 1 extra byte per read; ~40 B/s at 40 Hz |
| **Master code** | **new implementation needed** | **already exists** — `MiniHDLC::computeCRC16` / `crcUpdateCCITT` in RaftCore, 256-entry table | CRC-16 is *cheaper* here |

Because the CRC is accumulated in TXIS one byte at a time (§3.2), the per-byte cost competes
against a 90 µs wire budget rather than against an ISR deadline — which is what makes the choice
essentially free. A bitwise implementation is fine; a 16-entry nibble table (32 B flash) is
available if measurement ever shows it matters.

The detection strength is materially better and worth having on a measurement path: HD=4 up to
32751 bits (versus HD=2 for CRC-8 at 132 bytes), and a **1/65536 residual against random
garbage instead of 1/256**. That matters because a desynced burst *is* effectively random, and
at a 40 Hz poll rate a 0.39 % miss rate on corrupted bursts is not obviously negligible.

The independent plausibility checks still stack on top and should still be implemented — they
are nearly free and catch structurally-wrong frames before the CRC is even evaluated:

| Check | Constraint on random data |
|---|---|
| CRC-16-CCITT | 2⁻¹⁶ |
| `n ≤ 16` (scan) / `≤ 64` (fast) | ~2⁻⁴ |
| `mode ∈ {1,2}` | ~2⁻⁷ |
| `ch ≤ 3` | 2⁻⁶ |
| `seq == prev + 1` | 2⁻⁸ |

**Exact variant — CRC-16/CCITT-FALSE.** Poly 0x1021, init **0xFFFF**, MSB-first, no reflection,
no final XOR; `check("123456789") == 0x29B1`. This is precisely what `MiniHDLC::crcUpdateCCITT`
already implements (`(fcs << 8) ^ table[((fcs >> 8) ^ value) & 0xff]` with a `0x0000, 0x1021,
0x2042 …` table and `CRC16_CCITT_INIT_VAL = 0xFFFF`) — despite the header comment referring to
AVR's `_crc_ccitt_update`, which is the *reflected* variant. Name the variant explicitly in both
implementations or the two ends will silently disagree.

The **init value must be 0xFFFF, not 0x0000**. With init 0x0000 an all-zeros response computes
to CRC 0x0000 and would *pass* — so a silent slave, a bus held low, or a master reading past a
short response would all look like valid data. With init 0xFFFF the all-zeros 135-byte frame
computes to 0x57FD and is correctly rejected. This is a required property, not a preference:
zero-padding is a normal part of every response in this scheme.

### 3.5 Master-side configuration

New `pollInfo` keys, parsed in `DeviceTypeRecords::getPollInfo`:

```json
"pollInfo": {
  "c": "0x21=r135",
  "i": 25,
  "s": 38,
  "crc": { "t": "crc16ccitt", "trailer": 3, "reread": "0x22", "retries": 1 }
}
```

- `t` — `crc16ccitt`. Absent ⇒ feature off, current behaviour exactly.
- `trailer` — bytes appended; CRC region `L` = read length − trailer.
- `reread` — write-selector for the re-read command; absent ⇒ detect-and-drop only (the correct
  setting for a device with a non-destructive snapshot, e.g. the sound sensor, where the plain
  next read *is* the retry).
- `retries` — **Decision 2: configurable, adopted.** Re-read attempts before dropping the burst.
  Default 1. `0` = detect-and-drop. Keep it a per-device record value rather than a constant so
  the phase-5 bench data can set it from evidence rather than guesswork.

**Read-length headroom:** `RaftI2CCentral::MAX_I2C_READ_BYTES` is **240** and *silently truncates*
above that (`RaftI2CCentral.cpp:302`). 135 is comfortable, but the ceiling is worth knowing if
the burst is ever enlarged again — a request for more would be quietly clipped, not rejected.

Carried in `DevicePollingInfo`; enforced in `DevicePollingMgr::taskService` around the
`_busReqSyncFn` call.

**Do not put the check in the `resp.c` decode script** — that runs long after the bus
transaction, where retry is impossible.

---

## 4. Implementation phases

Each phase is independently shippable and independently testable.

| # | Phase | Repo | Deliverable | State |
|---|---|---|---|---|
| 1 | CRC-16/CCITT-FALSE primitive + host unit tests | RoboticalRSAO | `RSAOProtocol::crc16()`, `check == 0x29B1` | **Done** 2026-08-23 |
| 2 | Python reference + cross-check | MultiFirmware `tests/RSAOTester/host/rsao/` | `crc.py` mirroring `frame.py`, agreeing with C++ | **Done** 2026-08-23 |
| 3 | Slave: TXIS trailer + declared `L` in `I2CPerif` | MultiFirmware | `#ifndef BOOTLOADER_APP`; bootloader flash unchanged | **Done** 2026-08-23 |
| 4 | Slave: `0x22` REREAD in `VcpApp` | MultiFirmware | Selector + `seq`; declares `L = 132`; no FIFO pop | **Done** 2026-08-23 |
| 5 | Bench characterisation (pre-master-work) | — | Golden-pattern + noise tests, §5 | **Device validated on hardware** 2026-08-23; noise injection outstanding |
| 6 | Master: CRC validate + retry | RaftI2C + RaftCore | `pollInfo.crc`, `DevicePollingMgr`, counters; reuse `MiniHDLC` CRC | **Done** 2026-08-23 |
| 7 | Device type record → `r135` + `crc` block | AxiomDevTypes | Roll out after phase 3/4 firmware | **Done** 2026-08-23 — Axiom builds |
| 8 | Telemetry + fault injection + soak | both | Counters over REST, `0x3A CORRUPT_NEXT`, failure-path tests | **Done + validated on hardware** 2026-08-23 |
| 9 | Generalise to sound sensor | SoundSensorFW | `=r35`, `L = 32`, no `reread` (snapshot is re-readable) | |

### As-built notes (phases 1-4)

- **Files.** `RSAOAppCommon/I2CPerif/I2CCrc.h` (new, incremental CRC),
  `I2CPerif.{h,cpp}` (trailer state + TXIS), `RSAOComms.h` (forwarding setter),
  `VcpApp.{h,cpp}` (`0x22`, declared `L`); `RoboticalRSAO` `RSAOProtocol.{h,cpp}` +
  `test/test_rsao_crc.cpp`; `rsao/crc.py` + `tests/test_crc.py` +
  `tests/test_vcp_crc_trailer.py`.
- **No callback signatures changed.** Rather than adding an out-parameter to
  `RdI2CPerifResponseCB`/`RSAOCommsDeviceResponseCB` (three implementors: servo, VCP,
  bootloader), the app declares framing by calling
  `I2CPerif::setResponseFraming(L, seq)` from inside its response callback, forwarded
  via `RSAOComms`. This leaves the bootloader's code path literally untouched, which is
  what keeps C1 satisfied.
- **Bootloader flash verified unchanged: 5872 bytes, before and after.** Worth keeping
  as the CI assertion described in §5.2.
- **Cost.** VCP app flash 10684 → 11012 (+328 B, 46.8 % of 23552); RAM 1852 → 1868
  (+16 B, 47.8 %). Servo app +212 B flash, +8 B RAM. `TX_MAX` stayed at 132.
- **Empty responses keep the old behaviour.** `_txTrailerOn` is false when the callback
  returns 0 bytes, so an unhandled selector still reads back as all zeros rather than a
  trailer over nothing.
- **CONFIG declares `L = 16` explicitly**, even at offset `0x20` where only 4 bytes of
  the 36-byte block remain. Without that the trailer would land at bytes 4-6 of a read
  the raftjs cal read-back already performs (`12 00` / `12 10` / `12 20`, 16 bytes
  each), silently changing zero padding into trailer bytes. Covered by
  `test_config_last_chunk_pads_with_zeros`.
- **Test counts.** RoboticalRSAO host suite 72 tests (17 new, all passing); RSAOTester
  host suite 40 passing + 20 hardware-gated (16 new in `test_vcp_crc_trailer.py`).

### Bench validation (2026-08-23, real VCP via Axiom at 100 kHz)

All 16 hardware tests in `test_vcp_crc_trailer.py` pass against flashed firmware:
trailer valid on FIFO/STATUS/CONFIG/WHOAMI/serial/version, the first L bytes byte-identical
to the pre-trailer behaviour, and `0x22` reproducing the burst with an unchanged `seq`
and an unchanged FIFO entry count.

Soak (300 consecutive 135-byte FIFO reads plus 50 re-read trials):

| Metric | Result |
|---|---|
| CRC failures | **0** |
| Short reads | **0** |
| Sequence gaps | **0** |
| Re-read mismatches | **0 / 50** |
| Entries collected | 4800 (16 per read — the FIFO was saturated throughout) |

**ISR budget (C3) is fine.** Zero NACKs or short reads means the per-byte CRC keeps up
inside TXIS. Note what this does and does not show: the ~85 ms between reads was REST
round-trip bound (11.7 reads/s), so this is not a sustained *transaction-rate* stress
test. It is, however, the test that matters — the CRC has to keep up byte-to-byte
*within* each 135-byte transfer at wire speed, and it does.

The `enter_test_mode` bus lease suspends the Axiom's own 25 ms polling for the session,
so bench reads do not race the live poller (this matters: a poll between `0x21` and
`0x22` would otherwise overwrite the retained burst).

**Still outstanding for phase 5:** the noise-injection work in §5.4, which needs
physical rig changes (pull-ups, coupling, glitch injector). Worth doing *after* phases
6-7 so it can be measured with real detect/retry/drop telemetry rather than an ad-hoc
comparison script.

### As-built notes (phases 6-7)

Edited in the `raftdevlibs` clones so they can be committed from there.

- **RaftCore** — `DevicePollingInfo.h` carries the CRC config; `DeviceTypeRecords.cpp`
  parses `pollInfo.crc`. An absent or unrecognised `crc/t` leaves polling behaviour
  byte-identical to before, so this is inert for every device that does not opt in.
- **RaftI2C** — `DevicePollingMgr::validatePollResponse()` validates, re-reads up to
  `retries` times, and **strips the trailer on success** so the decode and `resp.b` are
  unchanged. A response that cannot be validated is dropped and the poll abandoned; the
  device is deliberately **not** marked offline, because this is corruption, not absence.
- **CRC reuse.** `MiniHDLC::computeCRC16` is exactly CRC-16/CCITT-FALSE (init 0xFFFF,
  `crcUpdateCCITT`), so no new CRC implementation was needed on the master.
- **Counters.** `DevicePollingMgr::CrcStats` (`checked` / `failed` / `recovered` /
  `dropped`), with a 30 s summary log that stays silent while nothing has failed.
  Surfacing these in device status JSON remains phase 8.
- **Build-generator fix (not anticipated in the plan).**
  `RaftCore/scripts/ProcessDevTypeJsonToC.py` cross-checks `resp.b` against the poll
  read length and **fails the build** on a mismatch — `Poll data size mismatch for
  RoboticalVCP JSON 132 Calculated 135`. Since the trailer is stripped before the decode
  runs, the invariant it is enforcing is right and the *generator* was what needed to
  know: `DecodeGenerator.poll_data_bytes()` now subtracts `crc.trailer` when a CRC is
  declared. Any future device adopting the trailer needs no further generator change.
- **`resp.b` stays 132** and the decode script is untouched; only `pollInfo` changed.

**Deployment ordering matters.** Once the Axiom runs this record it reads 135 bytes and
validates. A VCP running pre-trailer firmware returns three `0x00` bytes, which fails CRC
(init 0xFFFF, by design) and every poll is dropped — the device will look alive but
produce no data. Flash the VCP first, the Axiom second.

### As-built notes (phase 8)

**Why fault injection was needed.** After phases 1-7 every test still only proved that a
*correct* frame validates. The detect / re-read / drop path — the entire purpose of the
work — had never executed, on hardware or anywhere else, because a clean bus never
produces a bad frame. Waiting for real noise would have made the feature untestable and
unrepeatable.

- **`0x3A CORRUPT_NEXT <count>`** on the VCP flips one payload bit in the retained burst
  while the injection budget lasts, and un-flips it once spent. The count alone selects
  the master behaviour under test: `1` → the `0x22` re-read is clean, so the master
  should **recover**; `≥2` → the re-read is corrupt too, so it must **drop**. Always
  compiled in (~140 B) — testing the release image matters more than the flash.
- **`devman/busstatus[?busnum=N]`** (new RaftCore endpoint) reports each bus's
  `getBusStatusJson()`; `BusI2C` overrides it with
  `{"pollIntegrity":{"checked","failed","recovered","dropped"}}`. Neither
  `getBusStatsJSON()` nor `getBusStatusJson()` had any consumer before this, so the
  endpoint was the missing half.
- **`tests/test_vcp_crc_faultinject.py`** (6 tests) drives the **live poll loop**: it
  must *not* take the bridge's bus lease, which would suspend the polling being
  measured, so it uses the REST API directly. `devman/cmdraw` write-only works without a
  lease, which is what makes injection-while-polling possible.

The most valuable assertions are the two that were previously impossible:
`checked` advancing proves `validatePollResponse` actually runs on real polls, and
`failed == recovered + dropped` proves no failure silently reached the decode. A
clean-bus test guards the opposite risk — a false-positive CRC failure would discard good
data and look like noise rather than a bug.

#### Bench result (2026-08-23, real VCP + Axiom at 100 kHz)

Injecting 255 corrupt responses, the counters tracked the budget exactly — 128 failed
polls × 2 responses each (`0x21` + `0x22`) = 256 ≈ 255 — and produced **exactly one
`recovered`**: the single poll where the budget ran out between the read and the
re-read. That arithmetic is the mechanism confirming itself end to end.

| Window | checked | failed | recovered | dropped |
|---|---|---|---|---|
| clean bus, 60 s undisturbed | ~2100 | **0** | 0 | 0 |
| during injection | 59 / 53 / 63 | 55 / 53 / 20 | 0 / 0 / **1** | 54 / 53 / 20 |
| after injection cleared | 139 | **0** | 0 | 0 |

The device stayed responsive throughout and the bus returned to a clean steady state,
so sustained CRC failure plus re-read does **not** destabilise the slave. Throughput
during failures dropped from ~35 to ~29 polls/s, as expected when every failed poll costs
a second transaction — which is the real input for choosing `retries`.

#### Two findings that were not CRC bugs

**Corrupting the payload is not a fault injector.** The first implementation flipped a
payload byte before transmission. The probe showed the flip working perfectly and the
frame still verifying — because the CRC is accumulated in TXIS *from the bytes actually
shifted out*, so a pre-transmission edit yields a valid frame carrying different data.
The failing test was correctly reporting that a CRC accepts data it was computed over.
The fault has to be introduced at or after CRC computation, hence
`I2CPerif::setResponseCrcFault()`, which corrupts the *emitted* CRC.

**A reset RSAO stays in the bootloader indefinitely.** The master sends `START_APP` only
during *detection*, so a device that reboots after it has been identified is never
restarted and simply fails every poll. The bootloader answers WHOAMI/serial from the
same shared code but knows no app opcodes and emits no trailer, so this presents as a
100 % CRC failure rate that looks exactly like a CRC bug. This bit twice during
development. Root cause and fixes in §9 below.

#### Test-isolation constraints (both silent when violated)

- The `bridge` fixture is **session-scoped**, so one module taking the exclusive bus
  lease suspends polling for the entire run. The fault-injection module measures the live
  poll loop, so it drops the lease on entry and restores it on teardown.
- The arbitration tests deliberately reset the DUT, so any module needing the application
  must send `START_APP` itself.

Full suite on hardware: **65 passed, 1 skipped** (the skip needs a second DUT).

Order matters in two places: **3 before 7** (firmware must emit the trailer before the record
asks for it), and **5 before 6** (characterise the real error rate before designing retry
policy around it).

---

## 5. Test plan

### 5.1 Host unit tests (no hardware)

`RoboticalRSAO/test` already builds with CMake + g++.

- `crc16Ccitt("123456789") == 0x29B1` — pins the variant (poly 0x1021, init 0xFFFF, MSB-first,
  no reflection, no final XOR).
- **All-zeros rejection**: the 133-byte all-zero region computes to `0x57FD`, so a frame of
  `0x00` bytes with a `0x0000` trailer must be **rejected**. Guards against a silent slave or a
  bus held low reading as valid data.
- Every-byte-position single-bit flip over a 132-byte frame ⇒ **always** detected.
- All 2-, 3- and 4-bit error patterns ⇒ always detected (HD=4 holds well past 132 bytes).
- All burst errors ≤ 16 bits ⇒ always detected.
- Random-corruption Monte-Carlo ⇒ miss rate converges on 1/65536, proving the implementation is
  not accidentally weaker than the theory (a reflected/otherwise-wrong variant would still pass
  a naive round-trip test — this is the check that catches it).
- C++ and Python implementations agree over randomised vectors.
- Frame validator: header plausibility rejects `n > maxFit`, `mode ∉ {1,2}`, `ch > 3`.

### 5.2 Slave unit/bench tests

- **Bootloader flash regression** — build `bootloader_and_config_release_vcp_f030k6` and assert
  flash usage is *unchanged* from 5872 B. This is the guard on C1 and should be a CI check, not
  a manual step.
- **Trailer correctness** — read 135, verify CRC; read 132, verify the response is byte-identical
  to the first 132 (proves backward compatibility).
- **Declared-length padding** — with the FIFO partly full (`n < 16`), verify bytes
  `4 + n×entryBytes … 131` are `0x00` and that the trailer is still at 132–134. This is the
  regression test for the `L` mechanism (§3.1); getting it wrong is the most likely
  implementation bug.
- **Trailer on other selectors** — STATUS read of 11 (`L=8`+3) and CONFIG read of 19 (`L=16`+3)
  both carry a valid CRC, confirming decision 3 across the whole response path.
- **Re-read semantics** — `0x21` then `0x22`: identical bytes, identical `seq`, and FIFO entry
  count (via STATUS `0x20`) **unchanged** by the `0x22`.
- **Re-read isolation** — `0x21`, then STATUS and CONFIG reads, then `0x22` ⇒ still the original
  burst.
- **Sequence continuity** — consecutive `0x21` reads increment `seq` by exactly 1, wrapping cleanly.
- **ISR budget** — confirm no NACK-on-address-match regression (C3) at the 25 ms poll interval;
  the `INCLUDE_I2C_BUS_STATS` `nacks` counter must not move.

### 5.3 Known-answer corruption detection

Silent corruption needs a read whose correct answer is known. Three, no firmware change needed:

| Read | Bytes | Note |
|---|---|---|
| `0x38` WRITE_CAL → `0x12` CONFIG read | 16 | **Programmable pattern** — staging is RAM; never send the save (`FE 04 FE FE`) and flash is untouched. Set `0x55`/`0xAA` for worst-case SDA transitions |
| `0x13 <offsHi><offsLo>` flash read | 32 | Arbitrary bit patterns, deterministic |
| `0x99 0x04 0xFF 0x63` | 9 | Fixed `"Robotical"` |

Classify every transaction into **NACK/timeout** (visible), **payload mismatch** (silent — the
one that matters) and **bus hang**. Those three counts are the result.

### 5.4 Noise injection

Drive from the pytest suite through `RSAOTestMode` on the Axiom (no QT Py needed).

1. **Amplify susceptibility first** — pull-ups 2.2 kΩ → 10 kΩ, long unshielded harness. Slower
   edges mean far more time in the indeterminate region; this makes every later experiment more
   sensitive and may reveal errors with the product's own noise sources.
2. **Capacitive injection** — signal generator → 10–100 pF series cap → SCL, with ~100 Ω–1 kΩ
   series R to limit current. Sweep amplitude/frequency for a threshold. **Inject onto SCL and
   SDA separately** — SCL glitches desync the whole burst, SDA glitches corrupt one byte. Testing
   only SDA misses the failure mode that matters. Keep excursions within the pin abs-max.
3. **Deterministic glitch injection** — an RP2040/RP2350 PIO emitting a controlled-width
   low-going pulse after SCL rises, swept 50 ns → 1 µs. This directly validates the DNF setting:
   with `DNF = 4` (500 ns) pulses below ~500 ns should be rejected and above should desync.
   **Open-drain / pull-low only — never drive the bus high.**
4. **Realistic product noise** — servos and motors on the same harness, relay switching, harness
   hot-plug. Good for confidence and recovery testing; too uncontrolled for A/B.

### 5.5 Methodology

- **Measure a threshold** ("injection amplitude for 1 % transaction error rate"), not an error
  rate at one amplitude — coupling is brutally position-sensitive. Tape the rig down.
- **Interleave A/B runs** (CRC off / on, DNF 0 / 4), never all-of-one-then-the-other; ambient
  conditions drift over a session and will masquerade as an effect.
- **Include a negative control**: `CLK 400000` with DNF still at 500 ns *should* break (500 ns
  against a 600 ns minimum SCL high time). If the rig cannot detect a deliberately broken case,
  it cannot validate a working one.
- **Soak**: at 100 kHz a 135-byte read is ~13 ms, so ~75/s. Silent corruption is rare by nature;
  budget hours, not minutes.
- **Required end-to-end result**: with injection at a level that produces a measurable corrupted-
  burst rate, **zero corrupted bursts reach the decode path**, and the drop/retry counters
  account for every one of them.

### 5.6 Bridge gap

`RSAOTestMode` needs one addition: a **"repeat N transactions, compare against golden, return
counts"** command, so the timing loop stays on the device and the serial round-trip is not part
of the measurement.

---

## 6. Other recommended work

**Write-path CRC.** `0x38 WRITE_CAL` and `0xFF 0x40 …` are the dangerous writes because
corruption there is *persistent*, not one bad sample. Make the CRC optional and length-sniffed:
for a known-length opcode accept both `len` and `len+2` (trailing CRC-16). Backward compatible.
Reuse the same CRC-16 primitive rather than adding a CRC-8 alongside it — one implementation, one
set of test vectors, and the 1-byte saving on a 3-byte command is not worth a second primitive.

**Master-side counters as first-class telemetry.** Per-device `crcFail`, `rereadOk`,
`burstsDropped`, `seqGaps`, surfaced in device status. Without these the feature is invisible and
nobody will know whether the bus is healthy.

**Docs.** `MultiFirmware/docs/app-vcp.md` §2–§3 still documents *"at most 80 bytes … 9 frames
(scan) or 38 samples (fast)"* — stale since the 132-byte change (`4fe7837`). Update alongside the
protocol change rather than adding a second inconsistency.

**Pre-existing bug, unrelated but worth fixing while in this code.**
`I2CPerif.cpp:64`: `RCC->CFGR3 &= RCC_CFGR3_I2C1SW;` is missing a `~`. As written it clears every
*other* CFGR3 bit and preserves I2C1SW — the opposite of the intent. Benign today (the reset
value is 0 and `SerialDiags` selects `USART1SW_PCLK` which is also 0) but a landmine if any
peripheral ever needs a non-default clock source.

**DNF revisit if bus speed changes.** `I2C_DIGITAL_NOISE_FILTER = 4` is 500 ns, safe against the
4 µs minimum SCL high time at 100 kHz but *not* against 600 ns at 400 kHz. RaftI2C's per-device
`pollBusHz` can raise the clock at runtime while the slave's DNF is fixed at compile time. If any
device is ever moved to 400 kHz, this must drop to ≤ 2.

---

## 7. Decisions taken (2026-08-23)

1. **CRC-16/CCITT-FALSE from the start**, not CRC-8. Overhead is negligible on the slave and
   *negative* on the master (`MiniHDLC` already implements it). §3.4.
2. **Configurable retries** via `pollInfo.crc.retries`, default 1, `0` = detect-and-drop. §3.5.
3. **Trailer on all app-layer reads**, no per-selector exception list. Sole exclusion is the
   bootloader, on the C1 flash budget. §3.2.
4. **Uniform trailer everywhere** — supersedes the earlier idea of stealing reserved byte 29 of
   the sound sensor stats block. The sound sensor becomes `=r35` with `L = 32`.

### Still open

- **`retries` default per device** — 1 is a guess; set it from phase-5 data. A re-read costs a
  full extra transaction (~13 ms at 100 kHz) against a 25 ms poll interval, so more than 1 retry
  may not fit the polling budget.
- **Whether the master validates STATUS/CONFIG trailers or only FIFO data.** The mechanism is
  there either way; this is about how much master-side plumbing is worth building in phase 6.
- **Bitwise vs nibble-table CRC on the slave** — start bitwise; only revisit if the TXIS budget
  measurement (§5.2) says otherwise.

---

## 8. Prior art check

No existing document covers CRC error detection on the poll-data path. Searched
`RoboticalAxiom1`, `MultiFirmware` and `RoboticalAxiomSoundSensorFW`. What exists:

| Document | Relevance |
|---|---|
| MultiFirmware `devdocs/rsao-address-arbitration-as-built.md` | **Directly relevant precedent** — records that a table-less CRC-8 did not fit the bootloader flash budget, and that the SN check byte is an additive checksum, not a CRC (correcting a previously misleading comment). This is constraint C1 |
| `devdocs/rsao-arbitration-and-library-plan.md`, `RoboticalRSAO/README.md` | The `~(Σ−1)` framing checksum for bootloader commands — the *write* path, unrelated to read integrity |
| MultiFirmware `devdocs/i2c-slave-test-suite-plan.md` | The bench rig this plan's tests build on |
| `RaftCore/devdocs/unified-sampling-rate-and-polling-design.md` | Incidental "re-reading stale data" phrasing only |
| SoundSensorFW `devdocs/sound-sensor-rsao-development-plan.md` | "noise" only in the acoustic sense |

---

## 9. The bootloader latch, and the recovery backstop

Found while debugging phase 8: a device that resets never returns to its application, so
it fails every poll forever. Two independent fixes, one on each side.

### Root cause

`RSAOBootloader/BootloaderApp/BootloaderApp.cpp` auto-starts the application 10 s after
boot (`_sysTickCount > 10000`, a 1 kHz tick) **unless `_stayInBootloader` is set** — and
that flag is a one-way latch, never cleared. It was set in two places:

| Site | Condition | Effect |
|---|---|---|
| `i2cResponseCB` | **any** read with a selector byte | Every ordinary application poll pinned the bootloader |
| command path | any write of ≥4 bytes, **before** the checksum is verified | A corrupt or unrelated packet pins it |

The response-path condition is the damaging one: with a master polling at 25 ms, the
first poll after a reset latched the bootloader permanently, so the 10 s auto-start could
never fire. **The timeout is identical for reset and power-up** — there is no
reset-reason logic. It only ever elapses on a bench with nothing talking to the device,
which is why this survived unnoticed since the flag was introduced.

Git history: introduced in `e3491cb` (2022-12-04, *"Start application after 10s if no
commands received to bootloader"*) — the auto-start and the latch that defeats it landed
in the same commit. `148442b` (2022-12-28) narrowed the command path from "any data" to
"≥4 bytes" as a side effect of fixing a `dataLen - 2` underflow on 1-byte commands. The
response path was never touched.

### Fix 1 (device): latch only on reads the bootloader answers

```cpp
if (rxDataLen) {
    switch (pRxData[0]) { /* 0x50, 0x51, 0x52, 0x70 */ }
    if (txDataLen)                  // only a read this bootloader actually answers
        _stayInBootloader = true;
}
```

Application selectors now fall through untouched, so the 10 s auto-start works as
documented. **Costs zero flash** — still exactly 5872 bytes.

**The command path was deliberately left alone.** Narrowing it to checksum-valid frames
is correct but does not fit: measured **+4 bytes over a full FLASH region** against 272
free, and +12 for `|=` and ternary variants. (Curiously the cost is position-sensitive —
the same store placed after the checksum computation instead of before is free. Not worth
relying on.) The residual exposure is small: a ≥4-byte write to a board *already sitting
in the bootloader* is rare, whereas the response path fired on every poll. Fix 2 covers
the remainder.

### Fix 2 (master): record-driven recovery backstop

RSAO knowledge stays out of RaftI2C — the record supplies the bytes:

```json
"crc": { ..., "recover": "0x6004ff9c", "recoverAfter": 40 }
```

After `recoverAfter` consecutive dropped responses, `DevicePollingMgr` writes the recovery
command and resets the counter (`crcStats.recoveries` counts these). At 40 polls/s that is
about one second of complete failure before a rescue is attempted — long enough that noise
never triggers it, short enough that a reset device recovers quickly.

This also rescues units already carrying the old bootloader, which fix 1 cannot.

### Why both

Fix 1 is the actual bug and helps every RSAO regardless of master. Fix 2 is the only
thing that helps a device already in the field, and it generalises to any device that can
stop producing valid data — the mechanism has no RSAO-specific knowledge in it.

### Fix 1 verified on hardware (2026-08-24)

Flashed the combined image and sent `RESET_BOARD` while the Axiom polled throughout.
The Axiom was still running the **old** build with no recovery backstop, so the return to
the application can only be the bootloader auto-starting by itself:

| Window | Behaviour |
|---|---|
| t+0 → t+9.3 s | Bootloader — 100 % of polls failing CRC |
| t+10.9 s | Auto-start fires (19 of 49 polls failed) |
| t+12.4 s onward | Application running, **zero** failures |

The transition lands at ~10 s, exactly `_sysTickCount > 10000`. Before the fix this never
happened with a master present.

### Resolved: the intermittency was test state, not the fix

**Conclusion (2026-08-24): fix 1 did not cause it, and it is closed.** With both fixes
flashed the full suite ran green three times consecutively (66 passed, 1 skipped),
including the destructive recovery-backstop test. The account below is kept because the
first diagnosis was wrong in an instructive way.

Isolating the suspect arbitration test gave **17 passes to 1 failure**, so it was never
reliably reproducible alone. Reproducing it in full-suite context showed the failures had
migrated entirely to the *fault-injection* module, with `checked` frozen — the master was
not polling at all. The cause: the arbitration tests reset the DUT repeatedly, after which
the master must re-scan and re-identify it before it returns to the poll list, and that
gap is many seconds. The fault-injection module assumed polling was already live the
moment it dropped the bus lease.

Fixed by having `_live_polling` wait for `checked` to actually advance (30 s budget,
skip with a clear message otherwise) rather than assuming it. Note the failure mode this
removes: the assertion message said *"the master is not validating"*, pointing at the
master, when the real cause was the state a previous module left behind.

#### The original (superseded) diagnosis

`tests/test_delay_i2c.py` was green in full-suite runs before this change and is now
intermittent — observed **1 failure in 7** isolated runs of
`test_delay_i2c_stays_pinned_in_bootloader`, and an arbitration failure in 2 of 2
full-suite runs (a different test each time, plus cascading errors in later modules). The
failures did not reproduce on demand, so the detail was not captured.

The normative requirement still holds by inspection: `DELAY_I2C` is a *framed write* and
goes through the command path, which was **not** changed, so it still pins the bootloader
as spec §6.1 requires. The suspected mechanism is in test *setup* rather than the
behaviour under test — these tests reset the DUT and then need a pinning command to land
within the 10 s window, which previously *any* bus activity guaranteed and now only
specific commands do. That is a plausible but **unproven** explanation.

This needs resolving before the bootloader change ships: either harden
`_reset_into_bootloader` / `_ensure_pinned_bootloader` against a DUT that has auto-started
(re-reset and re-pin rather than skip), or revisit the fix. It is test debt exposed by a
behaviourally correct change, not evidence the change is wrong — but it is not yet closed.

---

## 10. Backward compatibility with pre-trailer firmware (2026-09-02)

### The problem, found on the bench

Two VCP boards on one Axiom slot: one running current firmware, one running firmware that
predates the trailer. The counters:

```
checked 29597, failed 3597, recovered 1, dropped 3596, recoveries 89
```

A 12 % failure rate, and `recovered: 1` out of 3597 — a re-read essentially never helped,
which is the signature of a *systematic* fault rather than noise. Reading each device's
serial (`0x01`, 8 bytes + 3-byte trailer) through the RSAO bridge identified it immediately:

| Addr | Serial | Trailer |
|---|---|---|
| 0x20 | `4437043530535871` | `0d 08 68` |
| 0x12 | `24c126312553311b` | `00 00 00` |

The device at 0x12 emits **no trailer**. Its every poll failed CRC, its every re-read failed
for the same reason, and after `recoverAfter` drops the backstop fired `START_APP` at an
application that was already running — 89 times.

> **This is the asymmetry that had not been noticed.** The *arbitration* serial read degrades
> gracefully with old firmware: `trailerVerified` is simply false and the check byte carries
> the decision (§11 of the RSAO address-assignment plan makes this explicit). The *poll* path
> did not degrade at all — a record that gained a `crc` block would fail 100 % of polls against
> every already-deployed unit of that type, forever. That made "add a CRC to every RSAO type"
> effectively a fleet-wide reflash, which was not the intent.

### The fix: classify by observation, per device

A pre-trailer device does not return corrupt data where the trailer should be. It returns
**zeros**, because it has no more data to clock out and the peripheral pads. That is a
recognisable signature, so the capability can be settled by watching the device rather than by
configuring it.

`DevicePollingMgr::RecoveryState` — the existing per-address table used by the recovery
backstop — gained a three-state classification:

| State | Meaning | Behaviour |
|---|---|---|
| `Unknown` | not yet decided | all-zero trailers accepted unchecked; counts toward a decision |
| `Absent` | `NO_TRAILER_CONFIRM_COUNT` (8) consecutive all-zero trailers | CRC checking **disabled** for this device; polls accepted exactly as before the CRC existed, and not counted in `checked` |
| `Present` | any non-zero trailer, or a passing CRC | **sticky**; every later CRC failure is real corruption |

**Per device, not per device type.** The record describes a device *type*; trailer support is a
property of an individual unit's firmware version. Nothing in the record needs to change.

**Sticky in both directions is the load-bearing part.** A naive "all zeros means no trailer"
rule would silently accept a response corrupted to all zeros — entirely plausible on I²C with
SDA stuck low. Once a device has proved it emits a trailer that escape hatch is closed for it,
so a genuine all-zero corruption is still caught.

`Absent` devices also never accumulate consecutive drops, so the recovery backstop stops
firing at them — which removes the mis-diagnosis in §9 for this case without needing the
`READ_VERSION` probe.

### Why 8

A trailer-capable device settles this on its **first** response: the `seq` byte alone is
non-zero for 255 of every 256 responses, and the CRC is non-zero almost always. So the count
only has to outlast a burst of corruption that happens to zero the trailer at exactly the
moment a device is first seen. Eight consecutive all-zero trailers at a 25 ms poll interval is
200 ms of a completely stuck bus, in which case nothing else is working either.

**Residual risk, accepted:** a trailer-capable device whose first 8 polls are *all* corrupted to
all-zeros is classified `Absent` for the rest of the session, losing CRC protection until the
next reboot. Requires a stuck bus during exactly the first 200 ms after detection.

**Known limitation:** entries are keyed by address and never released, so if a different device
later occupies a freed address it inherits the previous occupant's classification. With
`MAX_RECOVERY_ENTRIES` = 8 and a bench-scale bus this has not mattered; if address reuse becomes
common, clear the entry when a device goes offline.

### Measured effect

Same two boards, same bus, before and after:

| | Before | After |
|---|---|---|
| Poll failures | 3597 / 29597 (12 %) | **0 / 623** |
| Spurious `START_APP` recoveries | 89 | **0** |
| `RSAOTester` hardware suite | 63 passed, 4 failed | **67 passed** |

The old-firmware board keeps working, unchecked, with no reflash.

### A test-harness trap this exposed

Two of the four originally-failing tests — `test_persistent_corruption_is_dropped` and
`test_counters_are_consistent` — were *passing* in earlier runs for the wrong reason. Both
assert only "did `failed` increase?", and the old board's constant background failures satisfied
that without any injection reaching a device at all. Cleaning the bus turned them red and
exposed the real cause: `test_vcp_crc_faultinject.py` resolves its target from its own
`RSAO_VCP_DEVICEID` (default `1_10f`), *not* from `RSAO_DUT_ADDR`, so it was injecting into
address 0x0F where nothing was listening.

> **Set `RSAO_VCP_DEVICEID` explicitly** in whatever runs this suite. With a dirty bus these
> tests can go green without exercising anything.

With `RSAO_VCP_DEVICEID=1_120`, `RSAO_DUT_ADDR=20`, `RSAO_DUT2_ADDR=12`: **67 passed**,
nothing skipped — the first full-green run including the two-DUT `DELAY_I2C` decorrelation
tests, which need two real units.
