# Decision log

Every design change, why it was made, and the number behind it.

**How to use this.** Add an entry when you change something, at the moment you
change it. Three fields are mandatory: **what changed**, **why**, and **the
number** that justified it. A reason without a number is a memory; a reason with
a number is a decision someone can check, challenge or reverse.

Dates are left blank for entries reconstructed during design. Date everything
from here on.

---

## Hardware

| ID | Date | Change | Why | Number |
|---|---|---|---|---|
| HW-01 | — | Removed MicroSD card | Peak power over budget; also a common smallsat failure mode with unpredictable wear-levelling and no radiation screening | Peak 165 mW, but only 8 mW average — it was already duty-cycled to 5% |
| HW-02 | — | Removed RM3100 magnetometer | Not required for payload objectives; attitude is a bus function | 5.0 mW peak |
| HW-03 | — | Removed ISM330DHCX IMU | Same as HW-02 | 1.8 mW |
| HW-04 | — | **Net effect of HW-01..03** | Peak power reduced | **691 → 453 mW peak (−34%)**, 365 → 350 mW average (−4%) |
| HW-05 | — | TMP117 count 4 → 3 | Row size reduction | 5.2 B/row, 7.3% of file |
| HW-06 | — | TMP117 count 3 → 2 | Row size reduction | File 2,151 → 1,994 B |
| HW-07 | — | Added ADR4525 to an ADS1115 channel | Reference monitor doubles as a bit-alignment check on every record | 1 of 12 available muxed channels; costs 4 B/row |
| HW-08 | — | OPA388 count 4 → 5 | 3N163 A routed through a TIA, so five signals need five amplifiers | +9.5 mW on the 5 V rail |
| HW-09 | — | 3N163 split: one via TIA, one direct to ADC | Compares amplified against direct signal path | Two channels; the pair will not read alike for the same input |
| HW-10 | — | Memories moved to SPI, full duplex | Removes I²C bus contention with the ADS1115 and TMP117 devices | 32 kB scan: 0.74 s on I²C at 400 kHz vs ~26 ms on SPI |
| HW-11 | — | Photodiodes reclassified as DDD sensors | They measure displacement damage via dark-current drift, not illumination | Justifies 1 Hz: one eclipse gives 1,980 samples, a 44× noise improvement |

---

## Power

| ID | Date | Change | Why | Number |
|---|---|---|---|---|
| PW-01 | — | Corrected units error in the power budget | Every row multiplied by voltage twice, producing mW·V — not a physical unit | ESP32-P4 shown as 664.29 "mW"; correct value 201.3 mW |
| PW-02 | — | Buck converter modelled as a loss, not a load | A DC-DC is not a load; conversion loss was missing entirely | 50 mW peak, previously unaccounted |
| PW-03 | — | Added duty-cycle column | Peak power sizes regulators; orbit-average sizes the solar array. The second cannot be computed without duty cycle | Changed the headline figure from 691 mW peak to 365 mW average |
| PW-04 | — | Power tree defined: 5 V bus splits to TPS22918 A (→ 5V_SW analog) and TLV62569 (→ 3V3 → TPS22918 B → watchdog and processor) | Allows the analog section to be gated independently of the digital section | Analog rail is 52.3 mW of the total |
| PW-05 | — | Analog rail held permanently on, not duty-cycled | Cold-starting the TIAs before each reading risks warm-up offset drift imprinting an orbital-period artefact on the science data | Costs 52 mW; buys measurement stability |

---

## Data format

| ID | Date | Change | Why | Number |
|---|---|---|---|---|
| DF-01 | — | Output engineering units instead of raw codes | Requested for readability | Costs reprocessability: raw codes survive a calibration revision, converted values do not |
| DF-02 | — | Removed ESP32 housekeeping columns | Row size | 3,204 → 2,871 B per file |
| DF-03 | — | Temperature 4 dp → 1 dp | Row size | 2,871 → 2,511 B. Rounding error 0.05 °C, inside the sensor's ±0.1 °C accuracy — but discards 92% of distinct values |
| DF-04 | — | ADR4525 6 dp → 2 dp | Row size | 2,511 → 2,391 B. **Collapses the column to a single constant** — diagnostic value lost |
| DF-05 | — | Photodiodes 6 dp → 4 dp | Row size | 2,391 → 2,151 B, and **100% of distinct values retained** — 4 dp is finer than the ADC's 125 µV step |
| DF-06 | — | **Net effect of DF-02..05 and HW-05/06** | Row size reduction | **3,204 → 1,994 B per file (−38%)** |

---

## Storage

| ID | Date | Change | Why | Number |
|---|---|---|---|---|
| ST-01 | — | File period 30 s → 1 hour | LittleFS allocates whole 4 kB blocks; a 2 kB file wastes half of one | Slack **51% → 1.4%**; 30 MB lasts 2.7 → 6.15 days |
| ST-02 | — | Ring buffer sized from the partition | Logging must never stop; a full volume otherwise fails silently | 20% reserved for metadata; 6 MB partition holds 21 hours |
| ST-03 | — | Clean stored files at boot | Filenames restart each run and would collide with the previous run's | — |
| ST-04 | — | Explicit remove before open | `FILE_WRITE` truncates on the ESP32 FS layer but appends in the SD library — a re-run would silently double the rows | Behaviour now identical on both backends |
| ST-05 | — | Flush after every row | Buffering an hour of data means a power cut loses all of it | Bounds loss to one row |

---

## Protocol

| ID | Date | Change | Why | Number |
|---|---|---|---|---|
| PR-01 | — | Dropped CSP; plain-text framing | Simpler, readable in any terminal, no library dependency | Frame overhead 3 B vs CSP's 8 B plus KISS escaping |
| PR-02 | — | Fire-and-forget heartbeat, no ACK | OBC treats frame arrival as the liveness signal and sends nothing back | Retry becomes meaningless; CRC becomes advisory |
| PR-03 | — | Byte count and CRC32 placed in the `<END>` line | Lets the payload stream the body in one pass instead of scanning it twice | Enables `RESEND` of a 230 kB file with no RAM buffer |
| PR-04 | — | Block header: UTC replaced by boot_id + uptime_s + time_valid | The payload has no RTC. A clock it cannot keep accurate would produce timestamps that look authoritative and are not | Same 96-bit header, no size cost |
| PR-05 | — | Added `CLOSE` command | The open file cannot be resent — its length changes mid-stream — so without this the first retrievable file appears an hour in | — |
| PR-06 | — | Added `RESEND`, `LIST`, `STATUS` | Without recovery the stored files are write-only and a corrupt frame is unrecoverable | `RESEND` of an hourly file: 18.2 s at 115200 |

---

## Firmware

| ID | Date | Change | Why | Number |
|---|---|---|---|---|
| FW-01 | — | Seven dummy drivers with a uniform interface | Each mirrors a real driver, so swapping in hardware is a single-file edit | 170 kB of PROGMEM tables; n3163 stored sparsely at 0.9 kB instead of 7.2 kB |
| FW-02 | — | Decoupled transmit period from file period | Lets the heartbeat stay at 30 s while files roll hourly | Transmit reads from RAM, never from the file |
| FW-03 | — | Sampling decoupled from transmission | Previously all rows in a block were sampled at send time, so the header claimed 30 s spacing for simultaneous readings | Records now genuinely 30 s apart |
| FW-04 | — | Auto-start on power-up; STANDBY mode removed | The OBC's recovery action is a power cycle, which only works if power-on means running | Removes a state the payload could be stuck in |
| FW-05 | — | SAFE mode keeps transmitting | A silent payload looks dead and gets power-cycled regardless | Degraded frames at ~40 bps |
| FW-06 | — | Structs moved from `.ino` to a header | Arduino's preprocessor inserts function prototypes above the sketch body, so a struct declared in the `.ino` is invisible to them | Fixed `variable or field declared void` |
| FW-07 | — | Aligned table display for the serial monitor | Raw CSV rows are unreadable at 1 Hz | 91 characters wide, fits without wrapping |

---

## Mission and operations

| ID | Date | Change | Why | Number |
|---|---|---|---|---|
| MS-01 | — | Continuous operation → mission-scheduled sessions | Eight payloads share the spacecraft's power and downlink | Duty cycle no longer under payload control |
| MS-02 | — | Success criteria restructured into three levels | Most outcomes depend on schedule and downlink, neither of which the payload team controls | Level 2 counts **sessions**, not days |
| MS-03 | — | Objectives reorganised around SEU, DDD and TID | Three distinct radiation effects with different mechanisms and timescales | Moved DDD from secondary to primary |
| MS-04 | — | EIA considered and not baselined | It is a thermal-plasma phenomenon, not an energetic-particle one, and cannot cause bit flips | EIA electrons are ~7 orders of magnitude below the SEU threshold |
| MS-05 | — | Added eclipse-scheduling requirement | Dark current, and therefore DDD, cannot be measured in sunlight | A session entirely in the sunlit arc returns **no** DDD data |
| MS-06 | — | Added SAA-alignment scheduling request | Windows placed arbitrarily miss almost all transits | 1 h at a fixed clock time catches ~0.2 of 6; 20 min per transit catches all 5–7 |

---

## Bugs found and fixed

Recorded because the fix is less useful than knowing the failure mode.

| ID | Date | Bug | How it presented | Caught by |
|---|---|---|---|---|
| BG-01 | — | Power budget multiplied by voltage twice | Plausible numbers in a unit that does not exist | Review |
| BG-02 | — | Byte-to-field mapping sliced on the wrong boundaries | −11.1 °C decoded as +501 °C; both look like temperatures | Decoding against a reference |
| BG-03 | — | `rowsInFrame` never incremented | Zero heartbeats sent; board otherwise perfectly healthy | Simulated run |
| BG-04 | — | `maxFiles` read before assignment | Ring size resolved to 0 and pruned a file it should not have | Simulated run |
| BG-05 | — | `pgm_read_word` without a cast on signed tables | Compiles on AVR, fails on ESP32 | Compilation |
| BG-06 | — | Plot bucketed 30 rows into 60 columns | Empty buckets drawn at zero, looking exactly like a signal dropout | Visual inspection |
| BG-07 | — | Filename detection used `isdigit` on the first character | Filenames start with a digit, so a name was parsed as a count | Testing the command |
| BG-08 | — | **Test pattern written before scanning at startup** | Would silently erase every upset accumulated while the payload was powered off — the entire non-volatile exposure result | Review — see FW open item |

---

## Open items

Not decided. Each one changes something if resolved.

| ID | Item | Effect |
|---|---|---|
| OP-01 | PSRAM on-die ECC unconfirmed | If the PSRAM transparently corrects single-bit errors, the primary SEU objective measures zero for the whole mission while every counter reads normal |
| OP-02 | 3N163 intended function | If they are RADFETs, TID becomes a stated primary objective |
| OP-03 | FRAM and EEPROM part numbers | MB85R**C**256V and AT24C256C are I²C variants but the design shows SPI |
| OP-04 | TIA feedback resistance | Decides whether dark current is resolvable. 1 MΩ gives 8 ADC counts per nA; 1 GΩ gives 8,000 but saturates above 4 nA |
| OP-05 | ESP32-P4 current draw | Never measured. Bounding gives 35–85 mA, a factor of 2.4 on the figure that sizes the solar array |
| OP-06 | Downlink figures inconsistent | 1.13 Mbit/pass × 5 passes = 0.71 MB/day, against a stated 30 MB/day — a factor of 42 |
| OP-07 | Sleep residency no longer logged | Removed with the housekeeping fields. It is the only signal that light-sleep silently stopped working |
