# Start here

Updated 2026-09-15 on `claude/consolidate-2026-09-15`, the one branch every
earlier branch was folded into. Then read `HANDOFF.md` only where this file
points, `SHELL-GAPS.md` (what the shell owes) and `SEATS.md` (who does what).

## Goal

One Windows app that does everything the original MIDI++ window did and more.
The ImGui shell (`ui/`, `build\shell\MIDIShell.exe`) replaces the Win32 window
(`MIDI++/`, `x64\Release\MIDI++.exe`); both share `PlaybackCore` through
`ShellEngine`. Nothing the original had is optional (`SHELL-GAPS.md`).

## Seats

- Panel seat owns `ui/`, including `ShellEngine::Action` and `EngineSnapshot`.
- Claude owns `MIDI++/`, `tests/`, specs, and engine seams in `ui/`.
- Owner-approved exceptions, not precedents: Convert, sign-in, and the Convert
  popover rework (`DrawConvert`, 2026-09-15) in `ui/Panels.cpp`.

## Decisions made, do not reopen

- Velocity stays on a modifier (`VELOCITY_MODIFIER`); the tap is four events
  in one `SendInput` call.
- Injection never shares a thread with the message loop (`HANDOFF.md` section 4).
- Legit mode applies at dispatch, never at parse time (`LEGIT-MODE.md`).
- Timing numbers stop at the keyboard hook; never call them end-to-end latency.
- A skin chooses colour only; every skin is 1090 x 635 collapsed.
- Six built-in velocity curves, the sixth being S-Curve (`VELOCITY-CURVES.md`).
- MP3 to MIDI is a Python sidecar, never in process, bundled with a release.
- YouTube links use a signed-in session from `signin.py`; no third-party
  download service. The app never reads a browser's cookies itself.
- UI copy follows `HANDOFF.md` section 15; attribution follows section 13.
- Accent is for selection and fills; failures use `accent.bad`.

## Relevant files

- `ui/Panels.cpp`: `DrawConvert` is the Convert audio popover; the `+` menu
  above the file list opens it; `Panels::openConvert` is the render test's way in.
- `MIDI++/AudioToMidi.hpp`, `tools/mp3-to-midi/convert.py`, `signin.py`.
- `MIDI++/VelocityPresets.hpp`: the original editor's preset shapes, testable.
- `tests/run-shell-parity-mutations.ps1`: 20 deliberate regressions.
- `tools/make-release.ps1`: packages the shell; does not bundle the converter yet.

## Verified facts

- A running `MIDIShell.exe` blocks the shell link step (LNK1168).
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting a click.
- Never redirect a test exe with `*>` in PowerShell 5.1: a stderr line becomes
  a terminating error and the run stops with no failure printed. Run
  `RenderTests.exe` from its folder in cmd or bash if a script dies that way.
- Building `MIDI++.vcxproj` from another directory writes intermediates to
  `MIDI++\MIDI++\`, now ignored. Build from the repo root.
- Each worktree's `build\shell` needs its own `converter\.venv` junction to
  `D:\Dev\mp3converter\.venv` and its own sign-in (`cookies.txt`).
- The converter venv is 1.2 GB, 536 MB of it PyTorch; the winget FFmpeg full
  build is 638 MB, Deno 93 MB. A bundle needs `ffmpeg.exe` alone and a pinned
  package list, not a copy of the venv.

## Work completed, 2026-09-15

- Repository consolidated: every branch but `main` merged here; stale
  worktrees and local branches removed; the main checkout's uncommitted doc
  rewrite discarded (it said no task was queued; it was wrong).
- Salvaged: the `VELOCITY-CURVES.md` correction from `bc7d0df`, and
  `VelocityPresets.hpp` from a 2026-09-09 worktree, now fixing the editor
  presets that started at 0 (`VelocityPresetTests`, `preset-floor-at-zero`).
- Convert popover reworked: one primary action that becomes Cancel, quieter
  file picker, sign-in footnote, progress bar, status bar shows a running
  conversion, `+` marked while it runs. `convert` render scenario added.
- `SHELL-GAPS.md` and `MIDI-OUTPUT.md` corrected: `08aa1ab` built the MIDI
  output panel and the velocity modifier control. The panel prompt starts here.

## Unresolved

- **Panel seat, not started:** `PROMPT-S-CURVE-AND-SWITCHES.md` (mockup rename,
  drum and auto-transpose switches, export menu with styled and coloured sheets).
- **Release bundle:** `converter\` beside the exe with an embeddable Python, a
  pinned package list including pywebview, `ffmpeg.exe`, Deno, both scripts.
  Sizes above. Not started.
- **Needs the owner at the keyboard:** delivery into a game from the shell,
  Wooting feel, two devices at once, MIDI output into a real synth, live curve
  reconnection, a mixed-DPI move, and a look at the Convert popover live.
- **Standing question for the owner:** whether a typing warning comes back
  (`SHELL-GAPS.md`, "Reported by testers").

## Validation actually run

- At `f9b1b56`: `run-shell-parity-mutations.ps1`, 19 of 19 killed.
- At `358588d`: shell built; `ShellTests.exe` 35 PASS, 0 FAIL; `RenderTests.exe`
  260 PASS, 208 captures including 16 `convert`; `MIDI++.vcxproj` built.
- At `358588d`: `run-shell-parity-mutations.ps1`, 20 of 20 killed.
- Not run: native and latency tests, which take the cursor; nothing played into
  a game or a synth.

## Build and test

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' 'ui\MIDIShell.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
& .\tests\run-shell-tests.ps1 -Render
& .\tests\run-shell-parity-mutations.ps1
```

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus; `upstream` is Zephkek/MIDIPlusPlus.
  `main` stays at `e37ba7e`. Pushing without asking is authorized.
- Branches: `main`, `claude/consolidate-2026-09-15` (this), `astra/shell-parity`
  (the panel seat's old worktree at `D:\Dev\mpp-panels`, left alone).
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Hand `PROMPT-S-CURVE-AND-SWITCHES.md` to the panel seat, then start the release
bundle: extend `tools/make-release.ps1` to stage `converter\` from a pinned
package list and weigh the zip.
