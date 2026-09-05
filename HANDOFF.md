# MIDI++ successor: handoff brief

> New here? Read `CONTINUE-HERE.md` first. This file is the full brief.

**2026-09-05 update:** `CONTINUE-HERE.md` supersedes the implementation-status
claims below. The ImGui shell now has embedded/system fonts, DPI support,
file loading, Tracks with Solo Piano, and basic autoplay through a worker-owned
`PlaybackCore`. The personal fork is `K-Alexandru/MIDIPlusPlus`. The feature
design decisions in this brief still apply; statements below saying there is
no ImGui code or no personal remote describe the earlier state.

Context dump for continuing this work in a fresh session or a different assistant.
Written 2026-09-04. Updated after the keyboard-timing work built on `269c2f2`.
Section 16 and `LATENCY.md` contain the current measurement implementation and
test boundaries; they supersede the original timing assumptions in this brief.

---

## 1. What this is

A fork of [Zephkek/MIDIPlusPlus](https://github.com/Zephkek/MIDIPlusPlus), a
Windows MIDI-to-QWERTY converter and MIDI autoplayer for virtual piano games
(Roblox etc). C++20, Win32 + GDI+ UI, MSVC, single .exe.

**Goal:** rebuild it as its own app — new UI shell with skins and a mini mode,
user-visible latency instrumentation, a MIDI transport picker, and a
YouTube→MIDI pipeline — while keeping the inherited playback engine.

### Paths

| | |
|---|---|
| Repo | `D:\Dev\MIDIPlusPlus-modded` |
| Source | `D:\Dev\MIDIPlusPlus-modded\MIDI++\` |
| Build output | `D:\Dev\MIDIPlusPlus-modded\x64\Release\MIDI++.exe` |
| Shipped release being used | `D:\MIDI++ 1.0.4.R5 Release\` |
| MSBuild | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe` |

### Build

```
msbuild "D:\Dev\MIDIPlusPlus-modded\MIDI++.sln" /p:Configuration=Release /p:Platform=x64 /m
```

Requires C++20 and the Windows 10 SDK (C++/WinRT headers ship with it).
`windowsapp.lib` is needed for standalone C++/WinRT test programs, though the
main vcxproj currently links without it.

### License

**GPLv3.** Renaming and redesigning are permitted. Distributing builds requires
publishing source under GPLv3. It cannot be shipped closed-source.

---

## 2. The critical discovery

Upstream `main` is **a single squashed commit** (`512c4c7`, 2025-06-03). The
entire real history — 30 commits across 12 tags, 2024-08 to 2025-03 — is
**orphaned**, reachable only via tags, not from `main`.

`main` is newer by date but **regressed** relative to the `v1.0.4.R5_Rel` tag
(`ffc3622`, 2025-03-16). In the old history files sat at repo root; `main` moved
them into `MIDI++/`.

### What `main` lost, in `MIDI2Key.cpp`

| | `v1.0.4.R5_Rel` | `main` (fork base) |
|---|---|---|
| MIDI API | `Windows::Devices::Midi` (WinRT) | `RtMidi::Api::WINDOWS_MM` |
| Sustain cutoff | reads `g_sustainCutoff` (slider live) | `constexpr SUSTAIN_CUTOFF = 64` (slider dead) |
| Transpose | functional | `g_adjustedNoteMapping` stubbed to identity |
| Re-entrancy | none | `m_inCallback` guard that drops messages **and** clears a flag owned by the in-flight callback |
| Key tracking | atomic `pressed[]` + `modifierCounts[]` | non-atomic read-modify-write on `g_scancodeCount` — stuck-key source |
| Priority | thread-level only | process-wide `HIGH_PRIORITY_CLASS` |

Upstream's own R5 Rev 2 release notes say: *"Lower Latency for MIDI2Key — Now
runs on the newer WinRT API for Windows 10+"*. That work is absent from `main`.

**Unknown:** whether the revert was deliberate (WinRT may have had problems) or
lost in the squash. No commit message explains it.

---

## 3. Work completed

Branch **`input-path-r5`**, three input commits on top of `e37ba7e`, ending at
`269c2f2`, plus the measurement work in section 16. `main` untouched.

1. `739752b` — ported `MIDI2Key.cpp`/`.hpp` from `v1.0.4.R5_Rel`. Only fixup
   needed was dropping a stale `extern` declaration of `NtUserSendInputCall`
   (R5-era declared it as a function backed by the since-deleted
   `InputInjection.asm`; `InputHeader.h` now declares it as a function pointer).
2. `11100af` — wrapped `init_apartment(multi_threaded)` in the `MIDI2Key`
   constructor so `RPC_E_CHANGED_MODE` doesn't escape a constructor if the UI
   thread is already an STA.

Verified WinRT is genuinely linked by byte-searching the binary for the UTF-16
`Windows.Devices.Midi` activation string, `RoGetActivationFactory` and lazily
loaded `combase`.

### Committed as `269c2f2`, 2026-09-04: `IMidiInput` and Wooting analog

Working tree of the **main checkout** (`D:\Dev\MIDIPlusPlus-modded`), on
`input-path-r5`. Committed as `269c2f2`. Note this is not the `.claude/worktrees`
copy, whose branch predates the WinRT port.

New files:

- `MidiInput.hpp` / `MidiInput.cpp` — `IMidiInput` with `enumerate` / `open` /
  `close` and a callback of `(uint64_t timestampQpc, const uint8_t* data,
  size_t len)`. Backends: WinRT (`Windows.Devices.Midi`) and WinMM (through the
  vendored RtMidi). `EnumerateMidiInputs()` returns WinRT ports if there are
  any, WinMM otherwise, plus the Wooting device when present.
- `WootingAnalog.hpp` / `WootingAnalog.cpp` — a third backend reading the
  Wooting analog SDK directly (§14).

Changed:

- `MIDI2Key`, `MIDIConnect` — both now hold an `IMidiInput` and open by device
  id. Neither knows which transport it is using.
- `MIDIDeviceUI` — enumerates through the same interface, keeps the device list
  the combo was built from, and maps combo row to id. The old WinMM access test
  and its silent filtering are gone: a device that cannot be opened says so when
  it is opened, and filtering rows is what made the indices disagree.
- `MIDI++.cpp` — `g_selectedMidiDevice` (int) became `g_selectedMidiDeviceId`
  (`std::wstring`). All five open sites pass ids.
- `MIDIConnect` — the INPUT batch is now a local rather than a shared member, so
  the `m_inCallback` re-entrancy guard that dropped messages is gone, and the
  two mapping tables are explicitly zeroed (`wVk`, `time` and `dwExtraInfo` were
  indeterminate and were being handed to `SendInput`).

Builds clean. Verified on this machine with a standalone probe that links
`MidiInput.cpp`, `WootingAnalog.cpp` and `RtMidi.cpp`:

- WinRT enumerated the loopMIDI port and opened it.
- WinMM enumerated the same port as `winmm:0|MIDI 0` and opened it.
- The Wooting SDK bound at runtime, reported a connected device, and its poll
  thread ran and stopped cleanly.

**Updated verification:** loopMIDI through both WinRT and WinMM has passed real
note on/off, chords, sustain cutoff and velocity checks through MIDI2Key and the
Windows keyboard hook. **Notes confirmed playing in a real game, 2026-09-04, by
the author.** That settles the open question the whole port was blocked on: the
WinRT path stays, it is not reverted. Wooting playing, two physical devices at
once, and any measured game-side latency remain unverified.

---

## 4. Known bugs

### Device index mismatch: fixed 2026-09-04 in `269c2f2`

Three index spaces used to disagree: the combo listed WinMM devices minus the
ones that failed an access test, `MIDI2Key` indexed the WinRT collection, and
`MIDIConnect` indexed RtMidi's ports. Harmless with one device, wrong with two,
which is exactly the piano-plus-loopMIDI setup.

Devices are now identified by an opaque id from `IMidiInput` (§3) and the combo
and the open path share one list. Not yet exercised with two devices connected
at once, which is the test that matters.

### The re-entrancy guard in MIDIConnect: fixed 2026-09-04 in `269c2f2`

`m_inCallback` dropped any message that arrived while another was being handled,
because the INPUT batch was a shared member. The batch is a local now, so
concurrent callbacks cannot collide and nothing has to be dropped.

### Still present

- **ALT+key velocity preamble.** Partly reduced 2026-09-04: autoplay used to
  spend six events on a changed bucket against live input's four, because it
  tapped ALT through two separate `KeyPress` calls, releasing and re-pressing
  ALT inside its own tap. Both paths now send the same single four-event tap, so
  a plain note-on costs 1 event with velocity off and 5 on a changed bucket
  either way, 1 on a repeated bucket. Modifier mappings and volume adjustment can
  still add more. **The ALT protocol itself is untouched** and is still four
  events where one would do; removing it needs the target game's protocol
  checked first. These counts are verified; a dominant latency effect, or a
  specific ALT cause for upstream #44, is not. That issue has only a title.
- **UI thread vs injection.** Upstream's R5 notes admit: *"Windows limitation
  causes UI window dragging (WndProc messages) to conflict with NtUserSendInput
  syscall"*. Unfixed. Means the UI architecture is itself a latency factor.
  Hard requirement: injection must never share a thread with the message loop.
  (The Wooting backend already polls on its own thread for this reason.)
- **`MIDIConnect` 6.5MB table.** `m_noteMapping[128][128][10]` of `INPUT` as a
  class member, built in a 16384-iteration constructor, every entry differing
  only in two scancodes. Guarantees cache misses on the hot path.
- **`InputInjector.cpp` syscall stub.** Hand-built thunk to `NtUserSendInput`.
  Its speed benefit has not been measured. The syscall number is scraped from
  the current build's stub and the initial function
  pointer is a stub returning `69`, so any init failure silently no-ops every
  keystroke. Recommend reverting to `SendInput`.

### Twenty-second startup: fixed 2026-09-04

The splash screen was not decorative slowness. `rdtsc_timer_init()` calibrated
the TSC against QPC with `MAX_PASSES` 20 passes of `MEASURE_SEC` 1.0 second
each, every pass a busy-wait pinned to core 0. Twenty seconds of spinning to
measure a constant, before the window appeared.

Worse, it could not be configured away. `timer.h` captured both settings into
namespace-scope statics:

```cpp
static int    MAX_PASSES  = midi::Config::getInstance().autoplayer_timing.MAX_PASSES;
static double MEASURE_SEC = midi::Config::getInstance().autoplayer_timing.MEASURE_SEC;
```

Those initialise during static initialisation, long before the constructor calls
`loadFromFile("config.json")`, so they always saw the struct defaults on a
default-constructed singleton. `AUTOPLAYER_TIMING_ACCURACY` in `config.json` had
no effect whatsoever.

`rdtsc_timer_init(passes, measureSec)` now takes both as arguments, read at the
call site after the config is loaded, and the defaults are 5 passes of 0.1s.
Each pass is timed against QPC, which resolves the ratio far more finely than
the scheduling noise the median of several passes exists to reject.

Measured: test-suite wall time 23.6s to 4.1s. Scheduling accuracy is unchanged.
The legit-mode fixture measures an 800ms score at 797ms with the old 20-second
calibration and 796-797ms across three runs with the new one.


### Repo hygiene

Fixed 2026-09-04. 31 build artifacts were tracked (`.obj`, `.iobj`, `.pdb`,
`.exe`, `.tlog`), so any compile dirtied the tree and buried real diffs. The 29
intermediates under `MIDI++/x64/` are now untracked and ignored; they stay in
history if an old build ever needs reconstructing. The shipped `x64/Release/`
binary and its `.pdb` are still tracked deliberately, since that is what people
download. `/build/` is ignored for local verification builds.

---
## 5. Decisions made

| Decision | Choice | Reasoning |
|---|---|---|
| Keep the engine? | Yes | `PlaybackCore` (1858 lines), `MIDIParser`, `TranspositionCore`, `VelocityFrame`, `Track`, config format, key tables. Thousands of lines of working domain logic. |
| Fork or new app? | New app on an inherited library | The fork's own contribution is one commit confined to the UI shell. Upstream is dead — no history, issues unanswered since March 2025, nothing to sync with. |
| Audience | **Public release eventually** | Confirmed by the user. Means first-run defaults matter, and attribution is required (see §11). |
| New name? | **Undecided — "MIDI DEMO" is a working title** | The user explicitly does not want a suggested name influencing the decision yet. Do not pitch names. Renaming touches the title bar, About panel and repo, nothing structural. |
| Key mapping in Classic? | **Yes, both skins** | Earlier draft hid it in Classic for style. That was arbitrary — skins change appearance, not which features exist. |
| UI framework | **Dear ImGui** | The UI is mostly realtime custom-drawn visualisation (piano roll, 88-key mapper, velocity curve, event timeline, latency bars) — ImGui's home turf, Qt's chore. Keeps single-.exe deployment. Precedent: [ArijanJ/miditoqwerty](https://github.com/ArijanJ/miditoqwerty). |
| Runner-up | Qt Widgets | QSS skinning is first-class and it redraws only on change (ImGui redraws continuously, a real cost beside a running game). Rejected on 40MB+ deployment and custom-widget effort. Revisit if the app becomes text-heavy and conventional. |
| Rejected | Win32/GDI+ (status quo), WebView2/Electron/Tauri, wxWidgets, FLTK, .NET | Respectively: two layouts is unmaintainable; runtime dependency plus browser-child-HWND fights always-on-top-over-fullscreen; can't skin; can't skin; adds a runtime and interop for no gain. |
| Skins | Classic / Classic Dark / Modern / Modern Dark | One layout engine, four `ImGuiStyle` parameter sets. **Not** two layouts — that would mean placing every new feature twice and the skins would rot. |
| Mini mode | Supported in all skins | Compact utility windows are the *classic* Windows idiom, not a modern one. |

Design mockup: https://claude.ai/code/artifact/c0a9deb1-3f6e-4708-9215-8beb0bf7a6f8
Handoff prompt for another assistant: `HANDOFF-PROMPT.md` (untracked, repo root).

---

## 6. Open decisions

Genuinely still open:

- **Port `PlaybackCore.cpp` from `v1.0.4.R5_Rel`?** (+701/−323 vs `main`; R5 Rev 2
  notes claim the autoplayer was rewritten with "No More UI Lag".) Riskiest port,
  touches the fork's `Track.cpp` changes.
- **Scope of Wooting integration** (see §14).

Already answered, do **not** re-ask:

- **The custom curve's name and the default.** Resolved 2026-09-04: the user's
  tuned curve is named **Pro** and carries a "recommended" tag. The shipped
  default is **Linear Fine**, because a default should be unbiased rather than
  the best-sounding option; Pro is one click away in the preset list.
  "Radiant Grand" described the soundfont its author used and is retired.
- **Full-window height.** Resolved 2026-09-04: key mapping opens in its own
  window, the velocity editor starts collapsed, Tracks stays permanently
  visible. Rejected alternatives were tabs (biggest saving, but hides Tracks
  behind a tab, which is the discoverability failure that made Tracks get
  ignored in the first place), making every panel collapsible (saving depends on
  what happens to be collapsed), and going wider at ~1400px (fine on a
  widescreen, worse on a laptop). Measured in the mockup at a 1400px viewport:
  before 1078px Classic / 1210px Modern. After the layout change and the depth
  rebuild that followed (§15): 632px Classic / 725px Modern collapsed, 1006px
  and 1111px with the editor open. The key mapping window is 840 x 246px in
  Classic, 840 x 268px in Modern. Only the editor-open state exceeds a 1080p
  work area, and it is a setup state you leave.
  Consequences for the ImGui build:
  - The velocity panel header stays visible when collapsed and carries the two
    things you touch mid-session: the active curve and the sustain cutoff. The
    old separate sustain cutoff row is gone.
  - Key mapping is opened from a keyboard icon button in the primary strip, next
    to the settings gear, and closed from its own title bar. It has no place in
    mini mode. A text label there wrapped Modern's strip onto a second row, which
    is why it is an icon.
  - Whether the key mapping window is open should persist between sessions, and
    it should default to open on first run so the feature is discoverable.
- Audience: public release eventually.
- The app name: deliberately deferred. "MIDI DEMO" is a working title and no
  assistant should pitch a replacement.
- Mini mode contents: Live / Autoplay split, settled (§12).
- 32-step velocity editing: edit a smooth curve, sample to 32, show steps as a
  ghost (§12).

---

## 7. Recommended order

See `CONTINUE-HERE.md` for the short version and the build commands.

1. ~~**Play one note into a real game.**~~ Done 2026-09-04: notes confirmed
   reaching the game, so the WinRT port is validated and stays. Legit mode was
   listened to in the same session and judged unconvincing (section 12). Still
   open from this item: Wooting playing, and two physical devices at once.
2. ~~**Extract `IMidiInput`**~~: done 2026-09-04 in `269c2f2` (section 3). WinRT,
   WinMM and Wooting all sit behind it and devices are opened by id.
3. ~~**Build latency instrumentation.**~~ Implemented and tested in section 16.
4. ~~**Decide the ALT protocol.**~~ Closed 2026-09-04, won't fix. The cheap half
   was done in `08d07b4`: both paths now send the same four-event tap. Removing
   ALT is not possible. The velocity keys are
   `1234567890qwertyuiopasdfghjklzxc` and the piano mappings cover the same
   characters, so a bare velocity key plays a note. ALT is the only thing
   separating "set velocity" from "play a note", which is presumably why
   upstream chose it.
5. **UI rewrite** in ImGui. `skin-system.html` is the spec; zero ImGui code
   exists. Weeks, not hours.
6. **YouTube→MIDI pipeline.** Lowest coupling, can happen anytime.
7. **Legit mode, second attempt.** Only worth it if humanised autoplay actually
   matters to you. Section 12 has why the first attempt fails and where a second
   should start.

### Latency instrumentation: implemented with corrected boundaries

`t0` is the supplied backend QPC timestamp, `t1` precedes the first injection
call, `t2` follows the final call, and `t3` is the last tagged hook observation.
The hook can run before the call returns, as observed in the local tests.
Do not display `t3 - t2` as a positive Windows pipeline delay or stack overlapping
intervals. Show time inside calls, callback-to-hook time and event counts
separately. No game frame penalty or audio latency has been measured.
`LATENCY.md` explains the implementation, memory ordering and test commands.

### YouTube→MIDI pipeline

`yt-dlp` for audio, then **transkun** (better than Basic Pitch for piano, but
PyTorch with no clean ONNX path, so it stays a Python sidecar, never in-process).
Run as a queued background job and write the `.mid` into the `midi/` folder —
`ScanMidiFolder()` (`MIDI++.cpp:396`) already watches it, so the file appears
playable with no new plumbing. Add `FindFirstChangeNotification` for auto-refresh.
"Convert to sheet" is MIDI → VP text via the existing `player.full_key_mappings`.

Set expectations: transcription of arbitrary YouTube audio is lossy and rough for
anything that isn't solo piano.

---

## 8. Test rig (no piano required)

Already installed on this machine:

- **loopMIDI** (running, PID varies) — virtual MIDI port
- **teVirtualMIDI** driver — healthy
- `C:\Program Files\wooting-analog-midi\wooting-analog-midi.exe` 0.2.2
- `C:\Program Files\wooting-analog-sdk\wooting_analog_sdk.dll`
- `MidiPlayer6` in Downloads, plus a large `.mid` library

Chain: `MidiPlayer6 → loopMIDI port → MIDI++ MIDI2Key → keystrokes`. Tests
enumeration, note on/off, chords, sustain CC, velocity and stuck notes. Does
**not** test latency.

Note the user normally plays via Wooting analog → `wooting-analog-midi` →
virtual port → MIDI++. That chain has at least two software hops *before*
MIDI++, so perceived latency there is partly upstream of this app entirely.

A standalone WinRT-vs-WinMM enumeration probe was written and confirmed both APIs
see the same port and that `FromIdAsync` opens it. Re-run an equivalent probe once
a physical piano is connected — that's when enumeration differences would appear.

---

## 9. Layout reference (current fork)

From `MIDI++.cpp` `namespace Layout`, at 96 DPI:

- Window **1114 × 878**, fixed (`CLIENT_W`/`CLIENT_H` computed, not resizable)
- Edge padding 12, gap between cards 10, card padding 12
- Left sidebar 440 wide: "MIDI Files"
- Right column 640 wide, stacked: "Playback" (118h), "Advanced" (118h),
  "Config" (82h), "Details" (166h)
- Full-width below: "Tracks" (150h), "Log" (170h)

`Theme.hpp` is 84 lines of `constexpr COLORREF` in a **light** palette:
window `#F3F4F6`, card `#FFFFFF`, border `#DFE2E7`, text `#1C1E22`,
accent `#0078D4`. Type scale is 14/15/15/16px — effectively flat, which is why
nothing reads as more important than anything else. Note `Theme::UIBold()`
returns `FW_NORMAL` at the same size as `Theme::UI()`, so they are identical
fonts under different names.

---

## 10. Upstream reference

- Repo: https://github.com/Zephkek/MIDIPlusPlus (GPLv3)
- Relevant open issues: **#44** (shortcut-related title; ALT is an unconfirmed
  candidate cause), **#41** (autoplayer sends MidiConnect signals — the shared
  global conflict), #46, #40
- Prior art: [shizuhaki/miditoqwerty](https://github.com/shizuhaki/miditoqwerty)
  (Python, WinMM), [ArijanJ/miditoqwerty](https://github.com/ArijanJ/miditoqwerty)
  (C++, ImGui + SDL2 + PortMidi)
- A third-party unreleased app exists, known only from screenshots. Its
  KS/WinMM/WinRT selector plus user-tunable "Buffer Size (KS)" and "Buffer Count
  (KS)" suggests hand-rolled backends rather than Microsoft's Windows MIDI
  Services SDK — WinMM and WinRT are legacy *client APIs*, not transports of that
  stack, and it doesn't expose pin buffers. Treat as UI inspiration only.
- **"Legit mode" is dead config upstream, and was never fully restored.**
  Implemented for real in v1.0.1-v1.0.3: parse-time timing jitter, random note
  skipping and inserted hesitations. Removed outright in v1.0.4-1.0.4.R4.
  In v1.0.4.R5 and R5_Rel the config struct, its validation, its JSON
  round-trip and a `(Legit Mode)` / `(Normal Mode)` label on the details pane
  came back, but **nothing reads the values** - the label is the whole feature.
  `origin/main`, our base, keeps only the `LEGIT_MODE_SETTINGS` block in
  `config.json`: no struct, no validation, no label, no code. The serialiser no
  longer emits the key, so it disappears the first time the config is rewritten.
  Same shape as the WinRT regression in §3 - R5_Rel kept the shell and lost the
  substance. §12 has the re-add, built 2026-09-04 at dispatch rather than at
  parse; see `LEGIT-MODE.md`.
- **There is no Kernel Streaming backend anywhere in MIDI++.** `MidiBackend` is
  `WinRT | WinMM | WootingAnalog` and always has been. KS appears only in the
  third-party app's screenshots above and in our own mockup, where it is a
  planned option, not a present one.

---

## 11. Verified facts about the existing UI

Checked against the source. Do **not** redesign from memory — earlier drafts got
all four of these wrong.

| Fact | Evidence |
|---|---|
| **Cards and controls are rounded, not square.** Cards 8px, controls 6px. | `Theme.hpp:60-63` — `D_CORNER_RADIUS = 6`, `D_CARD_CORNER_RADIUS = 8`, drawn via `GpRoundRect`. |
| **There are no accent-filled buttons.** `ACCENT` is used *only* as a hover/press border. | `MIDI++.cpp:797-798`. Buttons fill `BTN_BG`; toggled buttons fill `ON_BG` (green). |
| **Sustain is one tri-state button, not three.** `IG → SPACE_DOWN → SPACE_UP`, cycled. | `PlaybackSystem.hpp:59-63` for the enum, `PlaybackCore.cpp:732-741` for the cycle, `MIDI++.cpp:783-790` for the tri-state colours (default / green / blue). |
| **`TrackControl` already draws real Mute/Solo buttons** with hover and press states, plus Channel, Program and NoteCount columns. | `TrackControl.hpp` — `DrawButton`, `m_muteButtonStates`, `m_soloButtonStates`, `enum class Column`. |

Other UI facts: window is **1114 × 878 fixed**; palette is light-only; type scale
is 14/15/15/16px (effectively flat); `Theme::UIBold()` returns `FW_NORMAL` at the
same size as `Theme::UI()`, so they are identical fonts under different names.

Current design mockup (v4):
https://claude.ai/code/artifact/c0a9deb1-3f6e-4708-9215-8beb0bf7a6f8

---

## 12. Agreed features not yet built

### Solo Piano — auto-mute non-piano tracks

The highest-value item, and nearly free. Motivating case: *Mipha's Court* has a
flute over piano; autoplay types the flute into the same 88 keys and the
arrangement collapses.

`TrackControl::TrackInfo` **already carries** `instrumentName`, `programNumber`,
`channel` and `isDrums`, so nothing new needs parsing. General MIDI programs
**0–7 are pianos** (Acoustic Grand through Clavi). The feature is:

```
mute every track where programNumber > 7 || isDrums
```

Add as a "Solo Piano" button in the Tracks header plus an "Unmute All". Also
worth a setting to run it automatically on file load — most virtual-piano users
want it every time and would never find the button.

### Tracks panel clarity

The user ignored this panel entirely because nothing said what it was for. Needs
an instrument-name column, a marker distinguishing piano from everything else,
muted rows visibly dimmed, and one line of explanatory text in the header.

### Velocity curve editor — the most important UI problem to solve

**Read this before designing anything here.** The user's diagnosis, verbatim in
substance: *the old editor made you drag each point up and down manually, and
"it's hard to know what's good or not or what to go for". Otherwise you just
click a preset like "smooth" or "s-curve". The design isn't intuitive or clear
about how it affects things, or what you'd even want if you're going for
something.*

That is **not a request for better drag handles.** The failure is that the UI
expresses the curve as *mathematics* when the user is thinking about a *musical
outcome*. Both the old options fail for the same reason: N draggable points give
total control with no guidance, and named presets give guidance with no control.
Neither tells you what the change will do to your playing.

Design the editor around four ideas, in priority order.

**1. Show the user's own playing on the graph. This is the key feature.**

Overlay a histogram of the velocities the user has *actually played* in this
session, underneath the curve. Suddenly "what should I go for" is answerable and
visible: most players' notes cluster in a narrow band (say 50–95), and what they
want is that band spread across the useful output range rather than squashed into
the middle. Without this, every other control is guesswork. With it, the goal
becomes obvious without anyone explaining velocity curves.

Also show a **live dot** for the incoming note's velocity, moving along the curve
as they play, with the resulting output value. Cause and effect, immediately.

**2. Two macro sliders instead of N control points.**

Most real needs are covered by two parameters, and two named sliders are far more
learnable than eight handles:

- **Sensitivity** — how hard you must press for a loud note. Shifts the curve up
  or down against the linear reference.
- **Contrast** — how sharply soft and loud separate. Flattens toward linear at
  one end, pushes toward an S-curve at the other.

Keep per-point editing as an "Advanced" affordance for people who want it, but it
must not be the primary interface.

**3. Show every preset as a shape, not a sentence.** (Revised 2026-09-04.)

Each preset draws its own curve as a thumbnail beside its name, so shapes are
comparable before selection rather than after. The user later removed the
written descriptions entirely: with the thumbnail, the live dot and the
histogram on the graph, a sentence under each name was restating the picture.

**4. A/B compare.**

One button to flip between the current curve and the previous one while playing.
Comparing is how anyone actually judges this, and it costs almost nothing to
build.

Supporting requirements:

- Large graph with a **dashed linear reference line** so boost vs cut is visible
- Duplicate / rename / new; the user's own custom curve is named **Pro**
- Not in mini mode — mini gets a dropdown to switch curves only

**Answer to the 32-step question** (the user clarified, so this is settled): edit
a **smooth curve**, sample it to the 32 steps, and draw the sampled steps as a
faint ghost under the smooth line. Editing 32 discrete steps by hand is exactly
the manual-dragging tedium that made the old one unusable. The steps are an
implementation detail and should be visible but not the editing surface.

### Key mapping widget

Its own window, available from **both** skins (§6). 52 white keys in a 640px
column is ~7px each, which is unusable, so the window is wider than the main one
(840px in the mockup) and shows one range at a time (e.g. C2–C6) with paging.
Black keys need real hit targets.

Opened from a keyboard icon button in the primary strip, closed from its own
title bar, and absent from mini mode. Its open state should persist between
sessions and default to open on first run.

**Piano keys must not follow the theme.** Ivory stays ivory and ebony stays ebony
in all four skins; only the highlight colour is themed. A piano is a physical
object, not UI chrome.

### Mini mode: Live / Autoplay switch

The user's idea and a good one. A switch inside mini changes what it shows:

- **Both:** Midi2Key, Velocity, Sustain, 88/61, latency status bar
- **Live only:** velocity curve dropdown, transpose
- **Autoplay only:** file selector (dropdown, not a list), transport, seek, time

File selection must work in mini — that was an explicit requirement.

### Skins

Four: Classic, Classic Dark, Modern, Modern Dark. **One layout engine**, four
`ImGuiStyle` parameter sets. Classic must match the real fork (8px/6px radii, no
accent fills, green toggles, Segoe UI). Modern needs genuine structural
difference, not just recolouring — borderless surfaces lifted off a deeper
ground, a real type anchor (22px vs Classic's 15px), heavier primary strip.
Depth via value contrast, since that is what an ImGui draw list renders cheaply.

Skins must be **runtime structs, not `constexpr`**, so a theme editor can mutate
and serialise them.

A **light/dark button belongs in the strip**, not in settings: a sun in light
mode, a moon in dark, switching between the two variants of the current skin.
The skin family (Classic or Modern) stays a settings-level choice.


### Legit mode — humanised autoplay, built 2026-09-04

Restored, at dispatch rather than at parse. `LEGIT-MODE.md` has the full
reasoning, the settings and the test results; the short version:

- `LegitModeSettings` is back in `config.hpp`/`ConfigHandler.cpp`, reading the
  `LEGIT_MODE_SETTINGS` block that was already sitting unread in `config.json`.
  Every field is optional on load, because configs in the wild have arbitrary
  subsets of those keys after upstream dropped the reader.
- **Legit Mode** is a checkbox in the Config card, so it can be switched
  mid-song. `toggle_legit_mode()` owns the live flag; the checkbox also writes
  `LEGIT_MODE_SETTINGS.ENABLED` so the two agree across launches.
- Effects are applied in the batch handler in `PlaybackCore.cpp`. The parsed
  score is never modified, so seek, the position readout and the reported
  duration stay exact, and each playthrough differs.

The three faults in the v1.0.3 version were all avoided rather than copied:

- **It could skip note-offs**, leaving a key held to the end of the file. The
  roll now applies to presses only, and the orphaned release is already a no-op
  because `release_key()` checks `pressed_keys`.
- **Its hesitation stretched the score** by accumulating into the running time.
  The delay now displaces only the notes it applies to. Measured: five forced
  60 ms pauses over an 800 ms score gave a span of **797 ms**, against the
  ~1040 ms accumulation would produce.
- **Its jitter was a random walk** on absolute time. It is now a per-press
  offset from the true schedule, late-only so no lookahead latency is added.

`TIMING_VARIATION` is a 0..1 scale over a 50 ms maximum spread rather than a
multiplier on the inter-event gap. 50 ms is the top of the
[30-50 ms asynchrony range](https://transactions.ismir.net/articles/10.5334/tismir.317)
measured in human piano performance.

**Verdict after listening, 2026-09-04: it does not convince.** The mechanism is
sound and the tests hold, but the output still does not read as human playing.
The author's assessment is that this is why upstream removed it. Kept, because
the plumbing is the expensive part and the tuning is cheap once someone knows
what to tune, but demoted: it is a checkbox in the **Config** card, not a button
in the Advanced strip. It should not sit beside the controls you reach for while
judging a mapping or a curve, given it drops notes on purpose.

What is likely wrong is the model, not the code. Three uniform draws with no
memory produce noise, and human timing is neither uniform nor independent:
[microtiming has long-range 1/f correlation](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC4174744/),
and expressive deviation tracks musical structure, arriving on phrase
boundaries and beat positions rather than at random. A future pass should
probably start from those two facts rather than from a wider spread.

---

## 13. Attribution (required, not optional)

GPLv3. Distributing builds means publishing source under GPLv3, preserving
copyright notices, and stating significant changes. Needs an About panel and a
README section crediting:

- **Zephkek/MIDIPlusPlus** — the base (GPLv3)
- **RtMidi** — MIT
- **Dear ImGui** — MIT
- Prior art worth acknowledging: shizuhaki/miditoqwerty, ArijanJ/miditoqwerty

First-run defaults should land on a working config: 88-key, velocity on,
sustain on, a sane curve. Not whatever the struct zero-initialises to.

---

## 14. Wooting analog input, built in

The user plays via a Wooting keyboard's analog keys, currently through the
separate [WootingKb/wooting-analog-midi](https://github.com/WootingKb/wooting-analog-midi)
app (Rust/Tauri, uses `midir`, unmaintained for years). They want it built in.

**This is a good idea for a reason beyond convenience: it shortens the latency
chain.** Today the path is analog key → wooting-analog-midi → teVirtualMIDI
virtual port → our app → SendInput. Two software hops and a virtual MIDI driver
sit in front of us, and we get blamed for their latency. Reading the analog SDK
directly removes all of it.

**Do not port the Rust app.** Call the same C API it calls. The
[Wooting Analog SDK](https://github.com/WootingKb/wooting-analog-sdk) exists
specifically to give native apps analog key support, and the user already has
`wooting_analog_sdk.dll` installed at `C:\Program Files\wooting-analog-sdk\`.
You need the wrapper header and import library from that repo; only the SDK DLL
is present locally.

**Licence: MPL-2.0, which is GPLv3-compatible.** Safe to link from this project.

Design notes:

- Analog key position (0.0–1.0) maps to note velocity. That is the whole idea:
  how far the key is pressed becomes how hard the note is struck.
- It becomes a **third input source** alongside MIDI2Key and MidiConnect, so it
  belongs behind the same `IMidiInput` abstraction from §7 rather than bolted on.
- The user reports **Improved Low Volume** is the best curve for Wooting, while
  the tuned **Pro** curve suits a real MIDI piano. Worth auto-suggesting a curve
  per detected input device.
- The upstream project has open issues worth reading before designing; fixing
  some along the way was explicitly part of the user's intent.

### Built 2026-09-04 (committed in `269c2f2`)

`WootingAnalog.hpp` / `.cpp` implement `IMidiInput`, so the analog keyboard
appears in the device list next to the MIDI ports and everything downstream
treats it as an ordinary input.

- **The SDK is bound at runtime** with `LoadLibraryW` plus `GetProcAddress`, so
  no headers, no import library, and the app still starts on a machine with no
  Wooting software. `wooting_analog_sdk.dll` at
  `C:\Program Files\wooting-analog-sdk\` exports the whole C API; verified with
  `dumpbin /EXPORTS`.
- **Keycode mode is ScanCode1**, which is what the rest of the app already
  speaks, so the note map is scancode keyed.
- **Poll thread at 1kHz**, `THREAD_PRIORITY_TIME_CRITICAL`, never the UI thread.
- **Thresholds**: a key counts as struck at 0.35 depth and can only be struck
  again after coming back through 0.20. The gap stops a key resting near the
  threshold from stuttering.
- **Velocity comes from press speed**, not depth. Depth cannot work on its own:
  every key crosses the threshold at the same depth, so depth at that moment is
  a constant. The current constant treats roughly 25 depth units per second as a
  firm strike. Untuned; needs a real keyboard and a real ear.
- **Default note map is white keys only**, C2 upward, following the row order
  the app already types (`zxcvbnm`, `asdfghjkl`, `qwertyuiop`, `1234567890`).
  Black keys are shifted characters in the app's own layout, and a shifted key
  is a different physical key to the analog SDK, so guessing that mapping would
  have been inventing behaviour. `SetWootingScancodeNoteMap()` exists to replace
  the whole table once that decision is made.

Verified: the SDK binds, reports a connected device, opens, polls and closes
cleanly. **No note has been produced through it yet** — that needs someone at
the keyboard.

Still open for §6:

- Where the note map comes from: the app's own key mapping inverted, a dedicated
  Wooting layout, or the key mapping window doing double duty.
- Whether analog depth should also drive something continuous (aftertouch, or
  the volume keys) rather than only choosing a velocity at strike time.
- Whether to auto-suggest a curve per input device, given the user reports
  **Improved Low Volume** suits Wooting and the tuned **Pro** curve suits a real
  piano.

---

## 15. UI copy and design rules

Adopted after the user flagged repeated failures. Apply these to every string
and control.

**Ask of every piece of text: does this help the user do something?** If it only
restates what the control already says, delete it. Examples that were removed:

- "Mute a part to stop it being typed out" on the Tracks header
- "Keeps loudness close to your input" as the Linear Fine description
- "Sustain · Space on press" — an invented label; MIDI++ just says **Sustain**
  and shows state through the tri-state colour

A description earns its place only when it tells you something the name does not,
and a picture beside the name can make even that redundant: the velocity presets
carry thumbnails and no prose at all.

**Other rules:**

- **No em dashes** anywhere in UI copy or documentation.
- **Stop uppercasing everything.** `text-transform:uppercase` on every panel
  header and label reads as machine-generated. Reserve it for genuine table
  column headers and small status badges.
- **Icons over words for utility controls.** Settings is a gear, not the word
  "Settings".
- **Do not invent behaviour and present it as existing.** Every label must trace
  to real code or an explicit decision. Several rounds were wasted on invented
  accent-filled buttons, square corners and a three-button pedal control, none of
  which exist in the fork (§11).
- **Interactive mockups must actually demonstrate the feature.** A static badge
  reading "WinRT" proves nothing about a transport picker; the control has to be
  clickable and visibly change something.

### Visual system, adopted 2026-09-04

The user supplied a full set of UI rules and asked for them to be followed, with
the caveat that colour scheme is ours to choose: do not copy a reference palette,
only fix genuine mistakes. The mockup now implements the following, and the ImGui
build should carry it over.

**Surfaces.** Five tones per skin, not one: canvas, structural (strip and status
bar), card, elevated control, and a **recessed** tone for anything you type into,
read from or fill up: fields, lists, the track table, the curve graph, slider and
progress tracks, segmented-control wells.

**Elevation.** Raised surfaces get a tight contact shadow plus a soft ambient
shadow plus a one-pixel top highlight. Recessed surfaces get an inner shadow.
Borders are semi-transparent hairlines (roughly 8% ink in light, 8% white in
dark), never solid greys. In ImGui: the ambient shadow is two or three stacked
translucent rounded rects, the contact shadow a one-pixel darker line, the
highlight a one-pixel lighter line inside the top edge.

**Spacing.** A strict 4px scale (4, 8, 12, 16, 24) with no one-off values.
Deliberate departure: container padding starts at 12px in Classic and 16px in
Modern rather than the 24px the rules suggest, because this is a dense
mouse-driven tool next to a running game.

**Radii.** Concentric. Classic 8px cards, 6px controls, 4px nested; Modern 12 /
10 / 8. Never the same large radius on a parent and its child.

**Hit targets.** 28px in Classic, 32px in Modern, not the 44px touch minimum.
Same deliberate departure, same reason. Every control in a row shares one height
so baselines align.

**Colour ratio.** Accent is reserved for selection, focus rings, slider fills and
the live curve, which is roughly the 10% the rules allow. Toggles stay green
because that is semantic state. Colour is never the only signal: an unmuted track
shows a speaker and a muted one a crossed speaker, and the row dims.

**Type.** One base size per skin (13px Classic, 14px Modern) for nearly
everything, 11/12px for metadata, one 15/20px title anchor. Weights 400, 500, 600
only. Hierarchy comes from contrast, not size.

**States.** Hover lifts the shadow, active presses the surface in and drops it a
pixel, focus draws a ring with an offset, transitions 160ms ease-out.

**Specific mistakes this replaced** (all reported by the user):

- The latency bar's empty track was invisible: it was one step off the status bar
  colour. Recessed tracks now have their own border and inner shadow.
- The transport picker sat at the top of the window and the latency it changes at
  the bottom. Latency now sits immediately beside the picker.
- Kernel Streaming's Buffer and Count fields were inserted inline and shifted
  every control to their right, wrapping Modern's strip onto a second row. They
  are a popover now: measured strip height is identical open and closed.
- The settings gear was a font glyph. All icons are now drawn on one 24px grid
  with a single stroke weight.
- Mute and Solo were the letters M and S. They are a speaker and headphones.
- The speed stepper's value was right-aligned with a min-width, so its right gap
  was smaller than its left. It is a fixed-width centred cell now.
- A timer moved the velocity dot at random in the mockup. Removed; the dot
  follows the pointer, and in the app it follows the notes played.

### Second UI pass, 2026-09-04 (same session)

**No status glows.** Every toggle had a green dot next to a green fill, and the
device chip had one too. They repeated what the fill already said. Removed
everywhere. The one dot that carried information, piano against everything else
in Tracks, became a small keyboard icon rather than a coloured circle, since
colour alone was never allowed to be the signal.

**Theme is its own control, not a setting.** A sun icon in light mode, a moon in
dark, switching the window between the light and dark variant of whichever skin
is active. A gear does not mean "theme", which is exactly why the old spoked
gear read as a sun and would have been mistaken for one.

**Icons are drawn, never font glyphs.** The gear is a toothed wheel; the sun,
moon, piano, speaker, headphones, transport and chevrons share one 24px grid and
one stroke weight.

**The piano shows no state it cannot explain.** Three keys used to be painted
red for no stated reason. Selection is the only highlight now: click a key,
press a QWERTY key, and the mapping changes. If a highlight cannot be produced
or cleared by the user, it does not belong in a mockup.

**A mockup has to be operable.** The current one is: strip toggles including the
tri-state sustain, per-track mute and solo with solo overriding mute, Solo Piano
and Unmute All, play, seek, speed, transpose, file selection, a settings panel
whose switches change behaviour (auto Solo Piano on load, velocity hotkeys
moving events-per-note from 1.4 to 1.0), key remapping, and the theme switch.
The status bar is computed from that state rather than typed in. A screenshot
cannot answer "what happens when I press this", which is the only question a
layout review is really asking.

### Third UI pass, 2026-09-04: the transport is not a main-window control

Decided: **the user should not have to pick a MIDI API.** The app defaults to the
lowest-overhead backend and the picker moves into Settings. Two of the three
options exist only to be worse than the default, so the chooser was holding the
best row in the window for a control most people touch once, if ever.

- The transport picker and the latency reading both left the strip. Settings now
  opens on a **MIDI input** group: one row per backend, each carrying its own
  figure, above a caption that changes with the selection, the Kernel Streaming
  buffer steppers, and the breakdown bar for whatever is selected. Kernel
  Streaming is the default row.
- Pass two's rule ("latency belongs beside the control that changes it") is not
  reversed, it is taken one step further: a reading you consult only while
  switching belongs next to the switch, wherever the switch ends up.
- The strip keeps the **device** name, which you do change, and the three icon
  buttons. Measured strip height is unchanged at 81 px.
- The status bar names the active input, so the choice stays visible without
  costing strip space. Mini mode dropped its latency figure for the same reason.
- The settings popover is 344 px wide and 544 px tall with Kernel Streaming
  selected, against 555 px from its top to the window bottom at 1090x635. It
  fits by 11 px. That is why the per-row descriptions collapsed into one caption
  for the selected row: three two-line rows overflowed the window by 76 px.

Two honesty notes carried into the mockup's own footer, because the panel as
drawn promises things the fork cannot yet do:

- **There is no Kernel Streaming backend.** See §10. Shipping this panel means
  writing it first; until then the default row is aspirational.
- **The per-backend figures need a measurement we do not have.** §16's
  instrumentation starts its clock at the backend callback, which is *after* the
  transport has done its work, so it cannot compare transports at all. Comparing
  them needs a loopback or an external reference, not the existing hook chain.

### Known mockup bugs worth remembering

An element toggled with the `hidden` attribute must not carry an inline
`display` style. Inline styles beat normal stylesheet rules, so the element stays
in layout while appearing hidden. Either use a class for the display value, or
declare `[hidden]{display:none!important}`, which does win over a non-important
inline style. Both are now in the mockup.

A second one, found and fixed while making the third pass: the mockup's `body`
rule had lost its opening line to a botched edit, leaving an orphan
`font-size:15px;...}` at top level that the CSS parser discarded. The page was
therefore inheriting the Artifact shell's fixed light `body` colours instead of
its own `--ground` and `--ink`, so switching the page to dark put theme-dark
cards on a permanently light background. Rule restored. The lesson is that a CSS
parse error is silent: nothing errors, the rule simply is not there, so a
stylesheet edit needs the same "check it in the browser" treatment as the markup.

## 16. Keyboard timing, committed 2026-09-04 on `input-path-r5`

Built on `269c2f2`, with an isolated Release x64 build at
`build\latency\app\MIDI++.exe`. Existing tracked release artifacts and the
shipped installation were preserved: the commit carries sources, tests and docs
only. The tracked `.obj`/`.exe`/`.pdb` files that an earlier default-output
build had already dirtied were deliberately kept out of it and are still dirty
in the working tree.

- `LatencyTelemetry.hpp`: header-only bounded MPSC ring, bounded consumer-side
  correlation, rolling history, nearest-rank median/p95/p99 and event counts.
- `InputLatency.hpp` / `.cpp`: opt-in hook on its own message thread, per-message
  correlation tags, local INPUT copies, call timing, partial/failure accounting,
  missing-observation expiry and clean close/reopen sessions.
- `InputLatencyWindow.hpp` / `.cpp`: **Measure latency** in the current Log
  header opens a functioning report for Live keys, MIDIConnect or Autoplay.
  Closing the window removes the hook. No ImGui work is implied by this viewer.
- MIDI2Key, MIDIConnect and PlaybackCore now provide trace scopes and use the
  measured wrapper. The original injection backend, event ordering, batches,
  velocity protocol and Wooting implementation are preserved. MIDI2Key publishes
  initial activation after preparing its mappings. Live handlers reject invalid
  8-bit data values before indexing their 128-entry tables. WinMM captures QPC
  at entry to its interface trampoline.

Verified with the real engine and a loopMIDI port: WinRT and WinMM note-on/off,
chords, zero-velocity releases, a sustain cutoff of 80, changed/repeated velocity
buckets, autoplay dispatch, MIDIConnect's ten-event protocol and tagged keyboard
hook observations. The fixture swallows its own tagged output after observing
it, so no test notes type into the focused app. A loopback readiness handshake
handles route reconnection while switching APIs.

Actual plain note-on event counts: 1 without a velocity change; 5 for a changed
velocity bucket in either path; 1 for a repeated bucket in either path.
Autoplay used to cost 7 for a changed bucket because it tapped ALT through two
three-event `KeyPress` calls, releasing and re-pressing ALT inside its own tap.
It now sends the same single four-event tap MIDI2Key does, so the two paths
agree and autoplay sends two fewer events in one fewer call. Removing ALT
altogether still requires checking the target's protocol; this change does not
touch it.

The queue stress test covers 120000 concurrent attempts and overflow accounting.
Tests also cover hook-before-return timing, out-of-order joins, missing hooks,
partial injections, the bogus stub return value, untouched shared INPUT tables,
and measurement disabled. The report window was driven through display, source
selection, close and reopen, and its rendered screenshot was inspected.

The Release build succeeds with existing warnings elsewhere in the engine.
The repeated-construction test also logs an inherited MMCSS registration warning;
that does not imply the priority setup is repaired. Process-wide priority tuning
and some UI-thread cleanup injection still exist in the source, despite earlier
claims that those paths were entirely thread-local.

Notes are confirmed reaching a real game (section 3). Wooting playing, sound
latency and two-physical-device selection are still unverified, and no latency
improvement is claimed from these functional tests. `LATENCY.md` has the design,
limitations, commands and raw-output locations. The UI rewrite remains a
separate stage.
