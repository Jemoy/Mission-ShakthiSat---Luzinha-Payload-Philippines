# SEED-P Payload — Mission Overview

What the mission is trying to find out, why the design serves those questions, and
what has deliberately been left out of scope.

This document states objectives. For how the payload is built and operated, see
[SYSTEM_OVERVIEW.md](SYSTEM_OVERVIEW.md).

---

## 1. Mission statement

SEED-P measures three distinct radiation effects on commercial electronics in low
Earth orbit, and how they vary with geomagnetic position:

| Effect | Sensor | Nature |
|---|---|---|
| **SEU** -- single-event upsets | 3 memory devices | Discrete events, one particle each |
| **DDD** -- displacement damage dose | 4 photodiodes | Cumulative lattice damage |
| **TID** -- total ionising dose | 2 MOSFET channels | Cumulative charge trapping |

Temperature is measured alongside them because two of the three effects are
strongly temperature-dependent and cannot be interpreted without it.

The spacecraft carries eight payloads sharing power and downlink, so SEED-P
operates in mission-allocated sessions rather than continuously. Within each
session it samples every channel at 1 Hz and returns a complete record; between
sessions it is unpowered.

Two of the three memory devices are non-volatile and therefore keep accumulating
radiation exposure while the payload is off — see section 5.5. That is what
allows a scheduled payload to return a whole-mission radiation result.

| | |
|---|---|
| Orbit | 500 km Sun-synchronous, i = 97.4 deg |
| Coverage | 15.2 orbits/day, full latitude range |
| Operation | Mission-scheduled sessions, shared with seven other payloads |
| Products | 1 Hz records of photodiode response, temperature and memory-upset counts |

---

## 2. Science objectives

### 2.1 Primary — map single-event upset rate against geomagnetic position

The orbit crosses three distinct radiation regimes on every revolution. The
objective is to resolve the upset rate in each, and the boundaries between them.

| Region | Encounters/day | Particle source | Expected signature |
|---|---|---|---|
| **South Atlantic Anomaly** | 5-7 | Trapped protons, inner Van Allen belt | Sharp, intense bursts |
| **Polar regions** | 30 (both poles, every orbit) | Galactic cosmic rays, solar energetic particles | Lower rate, varies with solar activity |
| **Quiet mid-latitude** | majority of each orbit | Background | Baseline against which the others are measured |

The SAA is the dominant contributor: the inner belt dips to roughly 200 km over
the South Atlantic, and proton flux at 500 km inside it runs two to three orders
of magnitude above the quiet background.

The polar contribution is the one a 97.4 deg inclination adds for free. Low
geomagnetic cutoff rigidity near the poles lets cosmic rays and solar particles
reach low altitude, and the orbit passes through both polar regions on every
revolution -- 30 crossings a day against 5-7 SAA transits.

**Deliverable:** upset count binned by geomagnetic latitude and L-shell,
resolving SAA boundaries and the polar horns, accumulated over the mission.

### 2.2 Primary — compare memory technology response

Three storage technologies are held under test simultaneously. They retain state
by different physical mechanisms and should therefore fail differently.

| Device | Mechanism | Array | Expected behaviour |
|---|---|---|---|
| PSRAM | DRAM cell, stored charge | 16 MB | Highest rate; provides nearly all events |
| FRAM | Ferroelectric polarisation | 32 kB | Intrinsically resistant; no charge well to disturb |
| EEPROM | Floating gate, stored charge | 32 kB | Low SEU rate; may show dose effects over months |

The arrays differ in size by a factor of 512 and in susceptibility by orders of
magnitude, so PSRAM is expected to produce essentially all observed upsets.

**A null result from FRAM and EEPROM is a valid outcome**, and a useful one -- it
bounds their rate for mission designers choosing storage. It is only publishable
if the record proves the scans ran, which is why scan coverage is logged
alongside the counts.

Because both are non-volatile they accumulate exposure while the payload is
unpowered, so this objective is unaffected by the operating schedule -- provided
the firmware scans the pattern before rewriting it. See section 5.5.

**Deliverable:** upset rate per bit per day for each technology, with confidence
bounds, and total device-days of exposure.

### 2.3 Primary — displacement damage dose in silicon photodiodes

Displacement damage is lattice damage: energetic protons knock silicon atoms out
of position, leaving defect sites. In a photodiode this appears as **rising dark
current** and **falling responsivity**, accumulating over months.

The measurement is therefore not the illumination curve. It is the **drift** in
two quantities across the mission.

| Observable | Measured when | Trend expected |
|---|---|---|
| Dark current | In eclipse, no illumination | Rises with accumulated fluence |
| Responsivity | Sunlit arc, against a repeatable reference condition | Falls with accumulated fluence |

Two device types are flown so that a divergence between them separates genuine
damage from a fault in one part.

**Why 1 Hz sampling.** Dark current is at the nanoamp level and a single sample is
dominated by noise. Averaging N samples improves the estimate by the square root
of N:

| Averaged over | Samples at 1 Hz | Noise improvement |
|---|---|---|
| 100 s | 100 | 10x |
| 15 min | 900 | 30x |
| **One full eclipse (33 min)** | **1,980** | **44x** |

One eclipse pass at 1 Hz yields a dark-current estimate 44 times less noisy than a
single reading. That is the justification for the sample rate -- not capturing the
illumination curve, which 1 sample per 30 s would resolve perfectly well.

**Temperature coupling is not optional.** Silicon dark current roughly doubles
every 8-10 degC:

| Temperature rise | Dark current |
|---|---|
| +10 degC | x2.2 |
| +20 degC | x4.7 |
| +40 degC | x21.8 |

An uncorrected dark-current reading says nothing about radiation damage, because
thermal variation swamps the signal. The TMP117 sensors are therefore **part of
the DDD instrument**, not housekeeping, and must be sampled simultaneously with
the photodiodes.

**Deliverable:** dark current and responsivity per device type, temperature
corrected, trended across the mission with stated fluence.

### 2.4 Supporting — thermal measurement

Two temperature sensors sample at 1 Hz alongside the photodiodes. This serves two
purposes.

**Primary purpose: correcting the DDD measurement.** As shown in 2.3, dark
current is strongly temperature-dependent. Without a simultaneous temperature
reading the damage signal cannot be extracted.

**Secondary purpose: thermal environment record.** The orbit imposes a cycle every
94.6 minutes with roughly 33 minutes in eclipse, producing several thousand cycles
per year -- an input to future payload thermal design.

**Deliverable:** temperature at each photodiode sample, and thermal cycle
amplitude and rate over the mission.

### 2.5 Secondary — commercial hardware in orbit

The payload is built from commercial parts with no radiation screening. Its own
survival is a result: watchdog resets, brownouts, storage faults and reference
drift are all logged and downlinked as engineering data.

**Deliverable:** anomaly log with reset causes, and reference stability trend.

---

## 3. Operating profile

SEED-P shares the spacecraft with seven other payloads. Power and downlink are
allocated by the mission, and the payload runs only when switched on.

### 3.1 What this means for each objective

| Objective | Effect of scheduled operation |
|---|---|
| 2.1 SEU mapping — PSRAM | Only samples while powered. Coverage depends entirely on **when** sessions occur, not how many hours they total. |
| 2.2 Technology comparison — FRAM, EEPROM | **Unaffected.** Non-volatile devices retain the pattern through power-off and accumulate upsets continuously. |
| 2.3 DDD — photodiodes | **Sessions must include eclipse.** Dark current cannot be measured in sunlight. A session entirely in the sunlit arc returns no DDD data. |
| 2.4 Thermal | Must be sampled simultaneously with the photodiodes, so it is captured whenever 2.3 is. |
| 2.5 Hardware survival | Unaffected; every session contributes reset and fault history. |

### 3.2 The payload requires no scheduling logic

The payload starts sampling automatically on power-up and stops when power is
removed. It holds no schedule, keeps no clock, and needs no command to begin.

This is why the design already suits scheduled operation without modification:
the OBC's power switch **is** the schedule. It also means a session can be
started, extended or cut short at any time without coordinating with the payload.

### 3.3 Session length matters more than total hours

| Session length | Orbit fraction | Full eclipse captured | Dark-current noise improvement |
|---|---|---|---|
| 20 min | 0.21 | Partial at best | up to 34x |
| 60 min | 0.63 | Only if timed onto eclipse | up to 44x |
| **95 min** | **1.00** | **Guaranteed** | **44x** |
| 3 h | 1.90 | Two eclipses | 44x, twice |

Four 20-minute sessions and one 80-minute session cost the same power and produce
the same data volume, but only the second guarantees a complete eclipse -- and
without eclipse there is no DDD measurement.

A 95-minute session guarantees eclipse regardless of when it starts. A shorter
session works only if the mission times it onto the eclipse, which requires the
scheduler to use the ephemeris rather than a clock.

## 4. Considered and not baselined

### 4.1 Equatorial Ionization Anomaly

The EIA was considered as a target and is **not baselined**, because the payload
carries no instrument that can observe it.

The EIA and the SAA are frequently mentioned together as "anomalies" encountered
in LEO, but they are different kinds of phenomenon:

| | South Atlantic Anomaly | Equatorial Ionization Anomaly |
|---|---|---|
| Physical nature | Trapped **energetic particles** | Enhanced **thermal plasma density** |
| Cause | Offset, tilted geomagnetic dipole brings the inner belt low | Equatorial fountain: E x B drift lifts plasma, which diffuses down field lines to form crests at +-10 to 20 deg magnetic latitude |
| Typical particle energy | MeV protons | ~0.1 eV electrons |
| Altitude of peak effect | Belt dips to ~200 km | F2 layer, ~300-400 km |
| Causes single-event upsets | **Yes** -- dominant SEU source in LEO | **No** |

A single-event upset requires one particle to deposit enough charge to flip a
memory cell. EIA electrons are roughly **seven orders of magnitude** below that
threshold. The memory experiment will not detect the EIA regardless of mission
duration -- not as a weak signal, but as no signal at all.

**What observing the EIA would require:**

| Instrument | Measures | Feasibility on this platform |
|---|---|---|
| Langmuir probe | Electron density and temperature, directly | **Feasible** -- a biased probe and an electrometer |
| Dual-frequency GNSS | Total electron content along the ray path | Moderate; needs a suitable receiver |
| 630 nm photometer | OI airglow at the EIA crests | Hard -- needs a narrowband filter, baffle and a far more sensitive detector than a bare photodiode |

The existing photodiodes cannot serve the third. Airglow is a few hundred
Rayleighs; an unfiltered, unbaffled silicon photodiode with a transimpedance
stage is orders of magnitude short of the required sensitivity and would be
dominated by stray light.

**Orbit note, should the EIA later enter scope.** The crests are strongly
local-time dependent, developing through the afternoon and evening. A
Sun-synchronous orbit has a fixed local time of ascending node, so the LTAN
choice determines whether the EIA is sampled near its peak or missed entirely. A
mid-morning LTAN samples it at a poor hour. This has to be settled at orbit
selection, not later.

### 4.2 Ionospheric plasma measurement

Not baselined -- see 4.1. The payload measures particle and damage effects on
electronics, not the ambient plasma.

### 4.3 Attitude-correlated science

Not baselined. The payload carries no IMU or magnetometer, so it cannot
determine its own pointing. Photodiode readings are therefore interpreted against
orbital position supplied by the spacecraft, not against measured attitude.

---

## 5. Success criteria

> **Success is judged on the quality of data the payload returns when operated,
> not on how often the mission chooses to operate it.**

The spacecraft carries eight payloads sharing power and downlink, so SEED-P
operates on a mission-allocated schedule rather than continuously. Session
frequency and duration are set by the mission, not the payload team. Criteria are
therefore written per session and per capability, not per day.

### 5.1 In-orbit success levels

| Level | Criterion | Threshold | Declares |
|---|---|---|---|
| **1 — Minimum** | Payload powers on and starts autonomously | 1 session | Survived launch; boots in orbit |
| | Complete file received on the ground | ≥1 file, CRC valid | Full chain works: payload → OBC → ground |
| | Sensor values plausible | Reference 2.56 V ±0.02 V, temperatures in range, photodiodes respond to light | Instrument functioning, not merely transmitting |
| **2 — Full** | Reliable operation across sessions | ≥10 sessions returning good data | Dependable instrument, not a one-off |
| | All channels alive | 0 dead channels | No sensor failure or degradation |
| | Memory scan recorded every session | 100% of sessions, **including zero counts** | Experiment ran, and can be proven to have run |
| | File recovery demonstrated in orbit | ≥1 successful `RESEND` | Data loss is recoverable |
| **3 — Stretch** | PSRAM upset rate stated | ~100 events accumulated | Primary science objective met |
| | FRAM / EEPROM exposure bounded | Device-days recorded; zero acceptable | Technology comparison complete |
| | Dark current measured in eclipse | ≥1 session containing a full eclipse | DDD baseline established |
| | DDD trend resolvable | ≥6 eclipse measurements spread over the mission | Damage accumulation observable |

### 5.2 What each level depends on

| Level | Depends on | Under payload control |
|---|---|---|
| 1 | One power-on and one downlink slot | Mostly — the payload does the rest |
| 2 | Being scheduled roughly ten times | Partly — reliability is ours, frequency is not |
| 3 | Session frequency, session duration, downlink volume, radiation environment | No |

Level 3 is deliberately marked as outside payload control. If the mission grants
two twenty-minute sessions, Levels 1 and 2 remain achievable and Level 3 does
not — that is a scheduling outcome, not a payload failure.

### 5.3 Pre-launch verification

Everything below is provable on a bench, with no spacecraft involved. These are
the commitments the payload team makes.

| # | Criterion | Verified by |
|---|---|---|
| P1 | Begins sampling within 60 s of power-on, no command required | Bench test |
| P2 | Valid frame every 30 s while powered, byte count and CRC correct | 24 h soak |
| P3 | Every sample written to storage; most recent N hours retained | Bench test on flight partition |
| P4 | Any retained file re-sendable intact on request | `CLOSE` + `RESEND` test |
| P5 | Heartbeat continues when a sensor or storage fault occurs | Fault injection |
| P6 | Reference channel holds 2.56 V ±0.02 V | Bench test |
| P7 | Power interruption does not corrupt stored data | Power-cut mid-write |
| P8 | Non-volatile memory pattern is **scanned before it is written** at startup | Bench test — see 5.5 |

### 5.4 Dependencies outside payload control

| Dependency | Owner | Needed for | If not met |
|---|---|---|---|
| Payload scheduled and powered | Mission / OBC | Everything | No mission |
| Frames received, verified, retained | OBC | Everything | Data lost at the interface |
| Orbital position supplied per record | OBC / bus | Spatial mapping of upsets | Counts become a rate, not a map |
| Downlink volume allocated | Ground segment | Any science return | Data stranded on board |
| Sessions include eclipse | Mission scheduling | **DDD measurement (2.3)** | No dark-current data at all |
| Sessions aligned to SAA transits | Mission scheduling | Efficient upset mapping (2.1) | See 5.6 |
| Repeatable session conditions | Mission scheduling | Trending DDD over months | Damage signal not separable from varying conditions |

### 5.5 Scheduled operation and memory exposure

Scheduling affects the three memory devices differently, because two of them are
non-volatile.

| Device | Volatile | Exposure | Yields |
|---|---|---|---|
| PSRAM 16 MB | Yes — pattern lost at power-off | Only while powered | High rate, **time and position resolved** |
| FRAM 32 kB | No | **Continuous, whole mission** | Low rate, **no timing** |
| EEPROM 32 kB | No | **Continuous, whole mission** | Low rate, **no timing** |

FRAM and EEPROM retain the test pattern through power-off, so upsets accumulated
while the payload was unpowered are still present at the next power-on. Those two
devices therefore receive full-mission exposure regardless of duty cycle.

**This only holds if the firmware scans before it writes.** Writing the test
pattern unconditionally at startup — the obvious implementation — erases every
upset that occurred while the payload was off, and silently discards the
continuous-exposure result. Criterion P8 exists to prevent this.

Expected accumulation for a 32 kB array, independent of duty cycle:

| Assumed rate | Upsets/month | Upsets/year |
|---|---|---|
| 1×10⁻⁷ /bit/day | 0.8 | 10 |
| 1×10⁻⁶ /bit/day | 7.9 | 96 |
| 1×10⁻⁵ /bit/day | 79 | 957 |

### 5.6 Scheduling requests

Two requests to the mission materially change the science return, at no cost in
power or downlink. Both are scheduling decisions, not design changes.

**Request 1 — place windows on SAA transits.** There are 5-7 per day and they are
predictable from the ephemeris.

| Scheduling approach | On-time/day | SAA transits captured |
|---|---|---|
| 1 hour at a fixed clock time | 1.0 h | **~0.2 of 6** |
| 20 minutes on each SAA transit | ~2.0 h | **all 5-7** |

**Request 2 — include a full eclipse periodically.** Dark current, and therefore
the DDD measurement, cannot be obtained in sunlight.

| Scheduling approach | DDD data returned |
|---|---|
| Sessions in the sunlit arc only | **None** |
| One eclipse-spanning session per week | Trend resolvable over months |
| Every session eclipse-aligned | Best statistics; costs no extra time |

Request 2 is the more critical of the two: without it objective 2.3 returns
nothing at all, whereas poor SAA timing merely slows objective 2.1. The two are
compatible -- a 95-minute session covers a full orbit and therefore captures both
an eclipse and any SAA transit within it.

### 5.7 Downlink cost per objective

Against whatever share of the ~370 kB/day payload allocation SEED-P receives:

| Product | Objective | Per session hour | Survives a small share |
|---|---|---|---|
| SEU records — addresses and counts | 2.1, 2.2 | 0.05 kB | **Yes** |
| Thermal, 1 per 5 min | 2.4 | 0.14 kB | Yes |
| Reference channel, 1 per 30 s | 2.5 | 0.6 kB | Yes |
| Photodiode + temperature, eclipse only, averaged | 2.3, 2.4 | 0.1 kB | **Yes** |
| Photodiode, 1 per 30 s, whole session | 2.3 | 3.0 kB | Yes |
| Full 1 Hz record | all, plus reprocessing | 205 kB | No |

**All three primary objectives are downlink-robust**, provided the DDD product is
reduced on board. The science quantity is a *single averaged dark current per
eclipse*, not the 1,980 samples used to compute it. Averaging on the payload or
the OBC turns 115 kB of raw eclipse data into about 100 bytes and loses nothing,
because the averaging is the measurement.

The full 1 Hz record is only needed if the data is to be reprocessed later --
for example, if the temperature correction is revised. It is the first thing to
decimate if the allocation is small, and the payload's on-board ring buffer holds
it for several days in case a specific eclipse needs retrieving.

## 6. Open items affecting objectives

| Item | Effect if unresolved |
|---|---|
| **PSRAM on-die ECC** | If the in-package PSRAM transparently corrects single-bit errors, objective 2.1 measures zero for the entire mission while appearing healthy. This is the single question that determines whether the primary objective is achievable. Confirm with the vendor before flight. |
| **3N163 function** | If these are RADFETs, dose measurement becomes a primary objective and section 4.2 is withdrawn. Their intended role should be stated. |
| **Downlink data product** | The payload produces 5.04 MB/day against a ~370 kB/day allocation. Which data reaches the ground determines which objectives can actually be met. Objective 2.1 needs only upset records and position, which fit easily; objectives 2.3 and 2.4 need the full-rate record and do not. |
| **TIA feedback resistance** | Determines whether dark current is resolvable at all. At 1 MOhm, 1 nA gives 8 ADC counts and a change is invisible; at 1 GOhm it gives 8,000 counts but saturates above 4 nA of photocurrent. The gain must suit dark current, not sunlight, or objective 2.3 cannot be met. |
| **DDD detectability in this orbit** | 500 km behind typical CubeSat shielding accumulates modest displacement fluence. Silicon dark-current change becomes clearly measurable at roughly 1e10-1e11 1-MeV-neutron-equivalent per cm2; a one-year mission may sit below that. A bounded null result remains publishable, but the expectation should be stated rather than a curve promised. |
| **Session scheduling** | Session timing determines both SAA coverage and whether any eclipse is captured. Without an eclipse-spanning session there is no DDD data at all. Raise at operations planning -- see 5.6. |
| **LTAN** | Fixed at orbit selection. Determines eclipse duration, which sets how many dark-current samples each session yields, and thermal cycle amplitude. |

The PSRAM ECC item and the scheduling item are the two that can quietly nullify
a primary objective while every indicator looks healthy.

The downlink item is the one that most constrains the science. Upset events are
sparse and compress to almost nothing, so the primary objectives survive a tight
budget. The full-rate photodiode and temperature record does not, and will need
either on-board averaging or selective retrieval from the payload's buffer.
