# STEM Is Hard
### And that's not a warning — it's the job description

*Presentation outline, 27 slides, ~25 minutes. Speaker notes in italics.*

*Slides 6–17 are one continuous section on a single mistake and what it teaches.
If time is short, slides 7, 8 and 11 can be cut without breaking the thread —
slides 13 to 17 are the part worth protecting.*

---

## Slide 1 — Title

# STEM is hard.

### Nobody tells you *why*.

*Open flat. Don't smile through it. The room expects a pep talk and they're not getting one, and that's the point.*

---

## Slide 2 — The lie you've been told

**"STEM is hard because it's complicated."**

That's not it.

Complicated things can be learned. You read the chapter, you do the problems, you get it.

**STEM is hard because you are wrong constantly, and finding out is the entire job.**

*Pause here. Let it be uncomfortable.*

---

## Slide 3 — What "hard" actually feels like

Not this:
- Struggling with a difficult concept for an hour, then understanding it

This:
- Building something for three weeks
- Being certain it works
- Discovering a single wrong number invalidates all of it
- Fixing it
- Finding the next wrong number

*Ask: has anyone here had that happen? Hands will go up. Good.*

---

## Slide 4 — A real project

**SEED-P** — a radiation experiment for a small satellite.

Measures how ordinary electronics break down in space.

Built by a student team. Real hardware. Real launch.

**Everything after this slide is a mistake that actually happened on it.**

*Not a hypothetical. Not a teaching example. Real, recent, documented.*

---

## Slide 5 — Mistake 1: the units

The team built a power budget. A spreadsheet. Careful work.

Every row multiplied by voltage **twice**.

```
61 mA × 3.3 V = 201.3   ← labelled "mA"
201.3 × 3.3   = 664.29  ← labelled "mW"
```

Milliamps times volts is milliwatts. Then they multiplied by volts again.

**That unit is not a thing. It doesn't exist.**

*This is not a stupid mistake. It's an easy one. That's what makes it dangerous.*

---

## Slide 6 — Mistake 2: the document that fell behind

The mission document said the payload measures:

- Magnetic field variations
- Motion and attitude from an IMU
- Time-stamps on every measurement

**None of those were true any more.**

Two sensors had been deliberately removed. The clock never existed at all.

*Here's the part that matters: removing them was the RIGHT decision. Made for good reasons, with real numbers behind it. The engineering was sound. The paperwork just never caught up.*

---

## Slide 7 — Why they were removed: power

Every satellite has a power budget. Yours is not negotiable — a solar panel makes
what it makes.

| Component | Peak power | What it did |
|---|---|---|
| MicroSD card | 165 mW | Extra storage |
| Magnetometer | 5.0 mW | Magnetic field |
| IMU | 1.8 mW | Motion and attitude |

| | Before | After removal |
|---|---|---|
| **Peak power** | **691 mW** | **453 mW** |
| Average power | 365 mW | 350 mW |

**34% less peak power.** That was the decision, and it was correct.

*But look at the second row.*

---

## Slide 8 — The number that surprised everyone

**Peak power fell 34%. Average power fell 4%.**

Why? The SD card only ran 5% of the time. It was huge when it ran — and it
almost never ran.

| | Peak | Average |
|---|---|---|
| MicroSD | 165 mW | **8 mW** |

Removing it made the *battery* happy and the *solar panel* barely notice.

**Two different numbers. Two different problems. Easy to confuse.**

*If you'd only looked at peak power, you'd think you solved something you didn't.
If you'd only looked at average, you'd think it wasn't worth doing. Both readings
are wrong on their own.*

---

## Slide 9 — Why more was removed: sharing the radio

The radio can send roughly **30 MB per day**.

The payload produces about **5 MB per day**.

So there's no problem — right?

**There are eight payloads on this satellite.**

*Pause. Let them do the division themselves.*

---

## Slide 10 — It's not speed. It's access.

You don't get a radio. You get a **slot**.

| | |
|---|---|
| Total downlink | ~30 MB/day |
| Payloads sharing it | **8** |
| Roughly available to us | **~3.75 MB/day** |

And the satellite is only in range of the ground station **a few minutes at a
time, about five times a day.** The rest of the day it's over an ocean with
nobody listening.

**You cannot send data whenever you want. You send it when it's your turn.**

*This is the part that surprises people. The bottleneck isn't the technology. It's
that seven other teams need it too.*

---

## Slide 11 — So we made ourselves smaller

Even with a share, less data means:

- More of it fits in each slot
- More days of backup stored on board when a slot is missed
- Room to spare when another payload needs extra

Six separate decisions, each one a small loss:

| Change | File size |
|---|---|
| Original record | 3,204 B |
| Remove housekeeping fields | 2,871 B |
| Temperature to 1 decimal | 2,511 B |
| Reference to 2 decimals | 2,391 B |
| Light sensors to 4 decimals | 2,151 B |
| Remove one temperature sensor | **1,994 B** |

**38% smaller** — and on-board storage went from holding **2.7 days** of backup
to **6.2 days**.

*Nobody wants to delete their own data. You do it anyway — because a measurement
that never reaches the ground isn't a measurement, and a missed slot shouldn't
cost you a week.*

---

## Slide 12 — The lesson

The design was **right**.

The document was **wrong**.

They drifted apart because updating a document produces no visible progress and
nobody schedules time for it.

**Then someone reads it and builds a plan around sensors that aren't on the board.**

*Your design will change. It should change — that's what learning looks like. But
every change leaves behind a document describing a thing that no longer exists,
and somebody downstream will believe it.*

---

## Slide 13 — The rule

# When you change something,
# you write down that you changed it.

Not later. Not "when we have time."

**At the moment you change it.**

*That's the whole rule. It sounds trivial. It is the single most-broken rule in
engineering.*

---

## Slide 14 — Why nobody does it

Writing it down feels like it costs you something.

| Doing the work | Writing it down |
|---|---|
| Visible | Invisible |
| Satisfying | Boring |
| Feels like progress | Feels like admin |
| Takes an afternoon | Takes two minutes |

So it gets skipped. Every time. By everyone.

**And then two months later somebody asks "why did we remove the magnetometer?"
and nobody remembers.**

*Not "nobody wrote it down properly." Nobody remembers. That's what two months
does.*

---

## Slide 15 — What a record actually needs

Three lines. That's it.

> **Removed:** magnetometer, IMU, SD card
> **Why:** peak power 691 mW exceeded budget; these three saved 172 mW
> **Date / who:** so someone can ask you about it later

**What changed. Why. When.**

*Notice the "why" line has a number in it. "We removed it to save power" is a
memory. "It saved 172 mW of a 691 mW peak" is a decision someone can check,
challenge, or reverse.*

---

## Slide 16 — Documentation is not paperwork

It's the only reason the project survives contact with:

- **Your future self**, who has forgotten everything
- **The next student**, who wasn't in the room
- **The team next door**, who is building against your interface
- **A reviewer**, who will ask why and deserves an answer

A project nobody can explain is a project nobody can continue.

**The build ends. The document is what's left.**

---

## Slide 17 — And this is a skill you can practise now

You don't need a satellite.

- Keep a lab notebook, and date every entry
- When you change your code, say what you changed and why
- When a number changes, write down the old one too
- When you decide *not* to do something, record that as well — future-you will
  wonder if you ever considered it

**People will notice this about you long before they notice how clever you are.**

*Genuinely. Reliability is rarer than brilliance and worth more.*

---

## Slide 18 — Mistake 3: the thing that looked right

Someone documented which bytes meant what. Reasonable-looking table.

The numbers were sliced on the wrong boundaries.

| Read as documented | Actual value |
|---|---|
| **+501 °C** | **−11.1 °C** |

**Both numbers look like temperatures.** Neither looks obviously wrong.

*This is the worst kind of error. Not a crash. Not an error message. Just plausible, confident, wrong numbers — for as long as nobody checks.*

---

## Slide 19 — Mistake 4: code that compiled and lied

The software sent a message every 30 seconds. It was tested. It worked.

One line was missing — a counter that was never incremented.

**Result:** zero messages ever sent.

The board looked completely healthy. Every light on. Nothing wrong to see.

*You would have found this by testing. Only by testing. Reading the code would not have found it.*

---

## Slide 20 — Mistake 5: nearly deleting the science

Two memory chips keep their contents with the power off.

That's the clever part of the experiment — radiation damage keeps accumulating while the payload sleeps.

The obvious way to write the code:

```
on startup:
    write the test pattern
    start checking for damage
```

**That erases every piece of evidence collected while it was off.**

Silently. Every counter still reads normal.

*Caught in review. One conversation away from losing a mission objective and never knowing.*

---

## Slide 21 — The pattern

Every single one of those:

- Was made by someone competent
- Was reasonable at the time
- Produced output that **looked fine**
- Was found by someone deliberately checking

**None of them announced themselves.**

*This is the thing. Being wrong doesn't feel like being wrong. It feels exactly like being right.*

---

## Slide 22 — So what actually makes it hard?

Not the maths.

Not the equipment.

**It's that you cannot trust your own confidence.**

Feeling sure is not evidence. The only thing that counts is checking — and checking is slow, boring, and produces no visible progress.

*This is why people quit. Not because it's difficult. Because it's relentless.*

---

## Slide 23 — The uncomfortable truth about experts

Experts are not people who stop being wrong.

**Experts are people who got faster at noticing.**

They've been wrong in more ways, so they recognise the shape of it sooner.

That's the whole difference. That's it.

*Say this slowly. It's the most useful sentence in the talk.*

---

## Slide 24 — What this means for you

If you're finding it hard, that is not a signal you're bad at it.

**It's a signal you're doing it.**

The people who find it easy are usually:
- Not checking their work, or
- Not yet working on anything that can be wrong

*Neither is a compliment.*

---

## Slide 25 — What to actually do

**Check the units.** Every time. It costs thirty seconds and catches the most expensive class of error there is.

**Test the thing, not your idea of the thing.** Code that compiles is not code that works.

**Write down every change, as you make it.** What changed, why, and the number
behind it. Two minutes now, or an unanswerable question in two months.

**Say "I don't know" early.** It's cheap now and expensive later.

**Let people check your work.** Every mistake in this talk was caught by someone looking. None were caught by the person who made it.

---

## Slide 26 — Why bother

SEED-P will fly. It will measure something nobody has measured in quite that way.

It will do that **because** people kept finding problems — not despite it.

Every mistake in this talk was a small disaster avoided. That's not the cost of doing the work.

**That is the work.**

---

## Slide 27 — Closing

# It doesn't get easier.

# You get better at being wrong.

### That's the whole skill.

*Stop. Don't add anything. Let them sit with it.*

---

## Appendix — if you have extra time

**Q: "How do you know when you're done?"**
You don't. You know when you've stopped finding problems, which is not the same
thing. Ship it anyway, and watch it carefully.

**Q: "Doesn't this mean nothing is ever reliable?"**
No — it means reliability is built by checking, not by being clever. Aeroplanes
are safe because thousands of people spent decades finding out how they fail.

**Q: "What if I make a mistake nobody catches?"**
You will. Everyone does. Build things that fail loudly rather than quietly, so
the mistake announces itself instead of hiding.
