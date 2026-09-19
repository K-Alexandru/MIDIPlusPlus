# Legit mode

Makes a human performance sound played live: not the same each time. Most of
what gets played is a human recording, and a listener in the game also sees the
input timing, so two playthroughs of one file should not be one key stream.

It is not there to make a poor or unplayable file sound real. There is no
repair, no thinning and no playability limit. A quantised file wants a looser
player, and the user picks it; the app does not guess what kind of file it has.

Off by default, and it wears an **Experimental** tag in Settings until the owner
says it convinces. Rebuilt on 2026-09-18; the first attempt and why it failed are
at the end.

## What a take is

The recording is the performance and a take is one evening's playing of it.
Every change is a small displacement around what the file already says.

| Amount | What it varies | At full scale |
|---|---|---|
| Timing | Each press, by its own few milliseconds | sigma 16 ms |
| Tempo | A slow drift ahead of and behind the score that comes back | sigma 45 ms |
| Dynamics | Velocity: a phrase-long lean plus a per-note part | 12 and 9 MIDI steps |
| Note Length | When a key comes up, as a share of the note's length | sigma 24% |
| Mistakes | A dropped inner note of a chord of three or more | 4% a note |

Each amount is a slider from 0 to 100%. Every random draw is clamped to three
sigma, so every offset has a bound.

**The assumed player** sets where the sliders sit and how hard difficulty bites:

| Player | Timing | Tempo | Dynamics | Note Length | Mistakes | Bite | Hesitates |
|---|---|---|---|---|---|---|---|
| Pro | 15 | 15 | 20 | 20 | 0 | 0.15 | never |
| Student | 40 | 35 | 45 | 45 | 30 | 0.8 | 30 to 80 ms, about every 40 s |
| Beginner | 75 | 70 | 75 | 70 | 70 | 1.6 | 60 to 180 ms, about every 10 s |

Pro is sized to how much one player differs between two takes of the same
piece, which is small: at the defaults a press moves by up to about 18 ms and a
release by up to about 47 ms over a minute of playing.

**Difficulty** is how much the piece asks of that player, 0 to 100%. The app
estimates it from notes per second, chord size and leaps at the speed being
played, and that is only where the slider starts: a mark labelled Estimated
stays on the track and the handle is the user's. Each amount is multiplied by
`1 + bite x difficulty x local`, where `local` is how busy the two seconds
around a note are against the song's own average, so variation and mistakes
gather where the song is hard. The tempo drift uses the song's difficulty and
not the moment's, so two neighbouring events are never moved by different
amounts.

**A hesitation** is a pause before a chord that is caught up over the next
second or two: a lag that jumps and decays, capped at 250 ms. It displaces, it
never stretches, so the song's length holds.

**A mistake** drops the press of an inner chord note, never the top or the
bottom, and never two within two seconds. Its release still runs and finds
nothing to let go.

## Why it is built ahead of time

`MIDI++/LegitTake.hpp` is a pure function of the score, the settings, a seed and
the speed. `prepare_event_queue()` calls it and writes the take into
`note_buffer`, so the dispatch loop plays a Legit song exactly as it plays any
other: it waits for the next event and fires it. Nothing on the dispatch thread
sleeps for Legit mode.

- **The score stays the ground truth.** `note_events` is never touched, and
  seek, the position and the duration read it.
- **Offsets may be early.** The plan knows every event in advance.
- **The toggle and every slider work mid-song.** They mark the take stale and
  the playback thread rebuilds it between two events, in milliseconds. It then
  lets go of any key the new take has already released or never pressed.
- **One seed per playthrough.** A new one when the song starts from the top, the
  same one through every pause, seek and rebuilt take, so a take resumes as the
  take it was. `legit_seed_override` forces a seed; only the tests set it.
- **Each amount has its own random stream** and draws for every event, so moving
  one slider rescales that amount and leaves the rest of the take where it was.
- **Offsets are wall-clock amounts.** At twice the speed the same 10 ms is 20 ms
  of score time, and a speed change rebuilds the take.

What the take guarantees, and the tests hold it to: a key is down for at least
15 ms, a key is up before it is struck again wherever the file had it up, two
tracks striking one key at one instant stay one instant, and velocity stays a
MIDI velocity.

## Hands, Trigger and Speed

These sit on the Playback card, because they change per song and mid-song. They
work with Legit mode off.

**Hands: Both, Right, Left.** Two tracks that both carry notes are the hands as
the file gives them, the higher one the right. One track is divided by an
estimate: two hand centres follow the playing, a chord wider than a hand is cut
at its widest gap, and the Hand Split slider in Settings, which also starts on
an Estimated mark, moves the boundary. The app never presses a key of the
silent hand, and it only ever releases a key it pressed, so the user can play
that hand themselves.

**Trigger: Auto, Hold, Tap.** Hold plays while its key is down and stops, keys
and pedal up, when it comes up. In Tap the clock stands still and each tap plays
the next note or chord with the take's own spread and velocities: the rhythm is
the user's and the touch is the recording's. Two keys can tap, so a run can
alternate between them, and each lets go of its own notes. **Hold Tapped
Notes** in Settings chooses between a note that lasts as long as the tap key and
one that keeps the recording's length. The pedal between two taps is run on the
way to the second.

The Hold and Tap keys bind in Settings under Hotkeys. They are registered as
global hotkeys, which keeps them from the game, and read for down and up by a
1 ms poll in the shell engine, because `WM_HOTKEY` never says a key came up and a
tap cannot wait for the engine worker's next pass.

**Speed** is a slider for everyone, 0.25 to 2 in steps of 0.05, and its value is
a button back to 1. It is a rate on the playback clock: a change is one store
that the playback thread picks up within a slice, keeping its place and its
held notes. It used to rewrite every event's time and restart playback.

## Remembered per song

**Remember per Song**, on by default, keeps the player, the five amounts,
Difficulty, Hands and the Hand Split for each file by name in `songs.json` beside
`config.json`. Off, one setting serves every song and Difficulty and the split
stay estimates. `config.json` holds the defaults under `LEGIT_MODE_SETTINGS`
(`ENABLED`, `PLAYER`, `TIMING`, `TEMPO`, `DYNAMICS`, `NOTE_LENGTH`, `MISTAKES`,
`REMEMBER_PER_SONG`) and `SHELL_HANDS`, `SHELL_TRIGGER`, `SHELL_TAP_HOLDS`.
Upstream's `TIMING_VARIATION`, `NOTE_SKIP_CHANCE` and `EXTRA_DELAY_*` still load
and mean nothing.

## What was measured first

`python tools\measure-midi.py <folder>` on eight of the owner's recordings,
2026-09-18: all eight are played (0 to 6% of onsets on a 1/48 grid), the median
chord spread is 9 to 20 ms in seven and 0 in one, same-instant chord notes are
0 to 15% in seven files and 81% in one, and velocities use 19 to 31 of the
game's 32 levels. So a per-note offset has to stay at a few milliseconds or it
rewrites the player's chords, and same-instant presses are not forbidden.

`run-latency-tests.ps1 -Fidelity x64\Release\midi` plays the opening of each file
through the real dispatch path: plain playback keeps a recording's timing to
under 0.4 ms at the 99th percentile and merges none of the onsets that are
under 3 ms apart. Playback was never what flattened a recording.

## Tests

```bash
./tests/run-latency-tests.ps1 -Legit
```

No MIDI hardware needed. The take itself: off is the score, bounded for the
loosest player, every press paired, only inner notes dropped, no key struck
while held, one seed is one take and another is another, the drift is slow
(lag-1 autocorrelation above 0.8), offsets double in score time at twice the
speed, and Hands by pitch and by track. Then the real dispatch path, reading the
keyboard hook: off is unchanged, dropped notes leave no key held, five notes
over 800 ms still span 800 ms, twice the speed is half the time with the score
not rescaled, and Tap in both note lengths with two keys.

`ShellTests.exe legit` covers the shell: a speed change that does not stop
playback or let go of a held note, Hands, settings recalled per song, Tap and
Hold through a key table, and what is saved. The `library` group holds that a
Load keeps the real flag, by the spread of five notes written at one instant.

**Not yet judged by ear.** The tests prove the take is bounded, ordered and
reproducible. Whether it sounds live is the owner's to say, and the tag stays
until he does.

## The first attempt, 2026-09-04

Three memoryless uniform draws at dispatch: a late-only press offset, a 2%
dropped note-on and a 50 to 200 ms hesitation on 5% of batches. The offsets and
the hesitation were `sleep_for` on the dispatch thread itself, so everything due
in that window waited behind them, and a tester called it laggy. Uniform noise
with no memory is not how timing varies: microtiming shows
[long-range 1/f correlation](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC4174744/),
which the tempo drift here stands in for with summed mean-reverting walks at 2,
8 and 32 seconds. Upstream's own v1.0.1 to v1.0.3 baked jitter into the parsed
timestamps, which made every playthrough identical and let each pause push the
rest of the song later; a 2:46 file gained about ten seconds. Its skip roll
could drop a release and leave a key held to the end of the file.
