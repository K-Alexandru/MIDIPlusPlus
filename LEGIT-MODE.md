# Legit mode

Makes autoplay sound played rather than typed. Off by default, and **it does not
currently work well enough to use** (see the verdict at the end). The **Legit
Mode** checkbox in the Config card toggles it, and it takes effect on the next
note: no reload, no restart.

Three effects, all applied when events fire:

| Setting | Effect |
|---|---|
| `TIMING_VARIATION` | Spreads the attacks in a chord and pulls single notes off the grid |
| `NOTE_SKIP_CHANCE` | Drops a note-on outright, a fumbled note |
| `EXTRA_DELAY_CHANCE` / `_MIN` / `_MAX` | Occasionally hesitates before a chord |

`config.json` holds them under `LEGIT_MODE_SETTINGS`. Every field is optional
when reading: upstream removed the reader for this block while leaving the block
in the shipped `config.json`, so configs in the wild carry any subset of the
keys, and a missing one falls back to its default instead of rejecting the whole
file.

`TIMING_VARIATION` is a 0..1 scale, not a millisecond figure. 1.0 spreads a chord
over `LegitModeSettings::MAX_SPREAD_MS` (50 ms); the 0.1 default gives 5 ms.
50 ms is the top of the range measured in real playing: asynchrony between notes
of a chord in human piano performance is typically
[30 to 50 ms](https://transactions.ismir.net/articles/10.5334/tismir.317).

Turn it off before judging a key mapping or a velocity curve. It drops notes on
purpose, and a dropped note looks exactly like a mapping bug.

## Why this runs at dispatch and not at parse

`process_tracks()` converts the MIDI into `RawNoteEvent`s and
`prepare_event_queue()` sorts them into `note_buffer`, an array of absolute
timestamps. The playback thread walks that array forward through `buffer_index`,
sleeping until each event is due and then firing everything due as one batch.
Seeking is a `lower_bound` over the same array.

Upstream v1.0.1–v1.0.3 baked the randomisation into those timestamps at parse
time. That is the smaller change and it costs nothing on the hot path, but:

- **The toggle would need a reparse.** The array is built once per file.
- **Every playthrough would be identical.** Restart the song and the same notes
  are missing in the same bars. One fixed set of mistakes is a fingerprint, not
  humanisation.
- **Error accumulates.** Jitter multiplied each inter-event gap and each
  hesitation was added to the running time, so the timeline was a random walk and
  every pause pushed the whole rest of the song later. At the shipped defaults a
  2:46 file gained roughly ten seconds and drifted against its own tempo map.

Applying the effects at dispatch keeps the parsed score as ground truth, so the
toggle works mid-song, seek stays exact, the position readout and the reported
duration stay honest, and each playthrough differs.

## The three rules that keep it out of the dispatch loop's way

**Offsets are late-only.** The loop only learns an event exists once it is due,
so it cannot fire one early. Symmetric jitter would need the whole schedule to
run a fixed distance behind — a permanent added latency, which is the one thing
this project is trying not to introduce. Late-only still breaks up the grid.

**Offsets never touch `total_adjusted_time`.** A hesitation displaces the notes
it applies to and nothing else. This is the difference between shifting the score
and stretching it, and it is what removes the accumulating drift.

**Only presses are displaced.** Releases fire on schedule, so a key is always
released before it can be pressed again.

Skipping needs no bookkeeping: `release_key()` only injects for a key that
`pressed_keys` says is down, so the note-off orphaned by a skipped press is
already a no-op. The 1.0.3 version had this backwards — its skip roll sat in the
branch handling note-on *and* note-off before they were told apart, so it could
drop a *release*, leaving the key held until the end of the file.

Hesitation is rolled once per due batch rather than per note. That avoids any
need to hold one event past another, and it is the more accurate model anyway: a
player hesitates before a chord, not before one finger of it.

## Reproducing a run

The generator is splitmix64, seeded from `__rdtsc()` at each song start. Set
`legit_seed_override` to a non-zero value to force a fixed seed, which makes a
reported run reproducible. Nothing in the app sets it; the tests do.

## Tests

```bash
cd build/latency/tests && ./LatencyTests.exe --legit
```

No MIDI hardware needed — it drives the real autoplay dispatch path with a
synthetic score and reads back what reached the keyboard hook. It also runs as
part of `run-latency-tests.ps1 -LoopbackPort <name>`.

Covered:

- With legit off, a three-note chord still produces exactly three presses and
  three releases, on the original code path.
- With `NOTE_SKIP_CHANCE` at 1.0, nothing is injected at all — the skipped
  presses take their orphaned releases with them.
- Over 40 notes at 0.5, presses are dropped and **every surviving press is
  released**: no key can be left held.
- With a forced 60 ms hesitation on all five notes of an 800 ms score, the
  measured span between first and last note was **797 ms**. Accumulating the
  pauses the way the parse-time version did would have given about 1040 ms.

**Verdict after listening, 2026-09-04: it does not convince.** The tests prove
the mechanism does not corrupt the score, drop releases or drift. They cannot
prove it sounds human, and it does not. It is kept and demoted to a checkbox in
the Config card rather than a button in the Advanced strip.

What is probably wrong is the model rather than the code. Three uniform draws
with no memory produce noise, and human timing is neither uniform nor
independent: microtiming shows
[long-range 1/f correlation](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC4174744/),
and expressive deviation follows musical structure, landing on phrase boundaries
and beat positions rather than at random. A future pass should start there, not
by widening the spread.
