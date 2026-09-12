# The built-in velocity curves: three options

Written 2026-09-09 because this is the oldest open item on `SHELL-GAPS.md` and
it is a decision before it is work. Changing any of it changes what the app
sounds like, so it is the owner's call.

**Decided 2026-09-11: option 2, implemented** with the tables below, and held by
`BuiltinCurveTests` and the `logarithmic-capped-again` mutation.

**Linear Coarse and Linear Fine are still open.** The owner asked for them to
be fixed. The R5 release's own executable was checked on 2026-09-11: its
built-in tables are byte-identical to these, so the original shipped the same
two curves offset by 2. Following the original therefore does not fix the
names, and what "fixed" should mean is a question back to the owner.

Everything below is measured off the real tables in
`PlaybackCore.cpp:getVelocityKey`, using the engine's own lookup rule: `idx`
advances while `table[idx] < target`, so `table[i]` is the largest input
velocity that still lands in output step `i`.

## What the tables do today

| Preset | Highest step reachable | Steps usable | Last interval | Typical interval |
|---|---|---|---|---|
| Linear Coarse | 31 of 31 | 32 | 3 | 4 |
| Linear Fine | 31 of 31 | 32 | 5 | 4 |
| Improved Low Volume | 23 of 31 | 24 | n/a | 5 |
| Logarithmic | 17 of 31 | 18 | n/a | 7 |
| Exponential | 21 of 31 | 22 | n/a | 8 |

Two separate findings, and the first is much larger than the report that
started this.

### Three of the five presets cannot play loud

`SHELL-GAPS.md` described Logarithmic repeating 127 fifteen times as "the flat
right-hand third of the graph", which reads like a cosmetic problem. It is not.
A repeated threshold names an output step no input can ever select, so:

- On **Logarithmic**, the hardest possible note comes out at step 17 of 31,
  55% of the range. The top 14 velocity keys can never be sent.
- On **Exponential**, it stops at step 21, 68%. The top 10 are unreachable.
- On **Improved Low Volume**, step 23, 74%. The top 8 are unreachable.

Whatever the player does, on those three presets fortissimo is not available.
That is a playability defect rather than a drawing problem, and it is the same
class of defect as the unreachable settings on this project's gap list: the
capability exists, the app never lets anyone get to it.

### Linear Coarse and Linear Fine are the same curve

They are the identical ramp offset by exactly 2 at every index below the top.
Both have 32 steps, so both have exactly the same resolution. The names promise
a difference in fineness that the numbers do not contain; what actually differs
is that Fine's thresholds sit 2 lower, so the same input lands one step higher
near the boundaries.

This is orthogonal to the ceilings and can be settled under any option below.

### The endpoint irregularities, which is what was originally reported

Linear Fine's last interval is 5 where every other is 4, and Linear Coarse's is
3. Both are rounding slack at the top of the range, worth about one step of
velocity on the very hardest notes.

## Option 1: leave the tables alone

Change nothing. The graph now draws each table honestly, including where it
saturates, so a user selecting Logarithmic can see the line stop at 55%.

- **Cost:** three of five presets keep a ceiling nobody chose, and the honest
  drawing makes it visible without making it fixable. A user who wants a
  logarithmic response and full range has no preset that offers both, and the
  answer becomes "build it yourself in the editor", which is exactly the
  "unreachable, so use the other app" shape this fork exists to remove.
- **Worth pairing with:** saying so in the UI, so a ceiling reads as a property
  of the preset rather than as the app failing to respond.
- **Nothing anyone has recorded changes.**

## Option 2: keep each shape, use all 32 steps

Resample the reachable part of each curve across the full output range. The
tonal character is preserved and the ceiling goes away. Linear Coarse and
Linear Fine are untouched, since they already reach the top.

    Improved Low Volume
      was      1  3  5  7 10 13 16 20 24 29 34 40 46 53 60 68 76 85 94 104 114 120 123 127 127 ...
      becomes  1  2  4  5  7  9 11 14 16 19 22 25 29 32 36 41 45 50 55  61  67  73  79  86  92  99 107 114 119 122 124 127

    Logarithmic
      was      1  2  3  5  7 10 14 19 25 32 40 49 60 72 85 99 115 127 127 ...
      becomes  1  2  3  4  5  6  7  8  9 10 12 14 17 20 23 27  30  35  39 44 49 55 61 67 74 81 89 96 105 113 120 127

    Exponential
      was      1  2  4  8 16 24 32 40 48 56 64 72 80 88 96 104 110 115 120 124 126 127 127 ...
      becomes  1  2  3  4  7 11 17 22 27 33 38 44 49 54 60  65  71  76  82  87  92  98 103 107 111 115 118 121 124 125 126 127

- **What a player notices:** the loudest playing gets louder on those three
  presets, and mid-range notes shift. Worst movement is 8 steps on Improved Low
  Volume, 14 on Logarithmic and 10 on Exponential.
- **Cost:** anyone who has tuned a performance against one of those three
  presets will find it plays differently, and Logarithmic changes most because
  it was the most truncated. There is no version flag for this, so it changes
  under them.
- **Requires no new judgement about what the curves should be**, only that they
  should span the range they are drawn against. That is the argument for it.

## Option 3: regenerate from the shape each name claims

Define each preset by a formula and derive 32 thresholds from it. Note the
table is the *inverse* of the response, so the shape has to be inverted rather
than applied to the table directly; doing it the wrong way round swaps
Logarithmic and Exponential for each other, which I did first and had to fix.

    Logarithmic, response log(1 + 9x) / log(10)
      becomes  1  2  3  5  6  8  9 11 13 15 17 19 22 25 27 31 34 37 41 45 50 55 60 65 71 78 84 92 100 108 117 127

    Exponential, response (e^2.3x - 1) / (e^2.3 - 1)
      becomes 14 25 34 42 48 54 60 65 70 74 78 81 85 88 91 94 97 99 102 104 107 109 111 113 115 117 119 120 122 124 125 127

    Linear Coarse and Linear Fine, response x
      becomes  4  8 12 16 20 24 28 32 36 40 44 48 52 56 60 64 67 71 75 79 83 87 91 95 99 103 107 111 115 119 123 127

- **Cost, and it is the reason I am not recommending this:** the formulas are
  mine, not anyone's tuning. `SHELL-GAPS.md` already refuses to ship the
  mockup's `x*x*(3-2*x)` as Pro's values on exactly this ground, calling it "a
  stand-in drawn to make the mockup legible, not a tuning". Inventing five of
  them has the same problem five times over.
- **Improved Low Volume is the clearest case against it.** Its name describes
  what its table does: fine steps at the bottom, 1, 3, 5, 7, 10, so quiet
  playing has resolution. A smoothstep would give it 13, 19, 24, 28 at the
  bottom, which is the opposite, and would make a preset named for low-volume
  resolution worse at low volume than Linear.
- **It does settle the Coarse and Fine question,** by making them the same
  curve openly rather than by accident. That is not worth the rest of it.

## What I would do

Option 2, and only for the three presets with a ceiling.

It fixes a playability defect rather than a cosmetic one, it needs no new
opinion about what a logarithmic response should feel like, and it leaves the
two linear presets exactly as they are. The endpoint irregularity that started
this is worth about one step at the very top and I would leave it, because
touching it changes Linear Coarse and Linear Fine for every existing user to
buy a difference nobody reported.

Under any option, the Coarse and Fine naming should be settled separately,
because right now the app offers two names for one curve and a user picking
between them is choosing on a promise the tables do not keep.

## What is needed to proceed

Which option, and whether the naming question is in or out of scope for it.
Option 2 is about an hour of engine work plus tests and a mutation, and it
changes what existing songs sound like on three presets, which is why it is
not already done.
