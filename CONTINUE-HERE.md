# Start here

Updated 2026-09-15 on `claude/continue-here-f0c3de`: `claude/continue-here-4d1c91`
(`7fcd008`) plus the inner shadow and Tracks spacing fixes (`83fa9ed`). Then read
`HANDOFF.md`, `SHELL-GAPS.md` (what the shell owes) and `SEATS.md` (who does what).

## Goal

One Windows app that does everything the original MIDI++ window did and more.
The ImGui shell (`ui/`, `build\shell\MIDIShell.exe`) replaces the Win32 window
(`MIDI++/`, `x64\Release\MIDI++.exe`); both share `PlaybackCore` through
`ShellEngine`. Nothing the original had is optional (`SHELL-GAPS.md`).

## Seats

- The panel seat, a second assistant in another app, owns `ui/`, including
  `ShellEngine::Action` and `EngineSnapshot`.
- Claude owns `MIDI++/`, `tests/`, specs, and engine-side seams in `ui/`.
- Owner-approved exception, not a precedent: Claude's Convert and sign-in
  actions, and the owner's UI fixes in `ui/Panels.cpp` and `ui/SkinDraw.cpp`.

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

## Relevant files

- `tools/mp3-to-midi/`: `convert.py` (file or link to `.mid`, one status per
  line), `signin.py` (WebView2 sign-in, writes `cookies.txt`), `README.md`.
- `MIDI++/AudioToMidi.hpp`: finds the install, runs either script in a job;
  `AudioToMidiTests` in `tests/ShellTests.cpp` covers it.
- `ui/ShellEngine.cpp`: `ConvertAudio`, `ConvertCancel`, `ConvertProgress`,
  `YouTubeSignIn`.
- `ui/Panels.cpp`: the + menu and Convert audio popup, file list, Tracks panel.
- `ui/SkinDraw.cpp`: `InnerShadow`, `RecessedRect`, `RoundCorners`.

## Verified facts

- Transkun and mp3converter are MIT; `convert.py` copies no code from either.
- 93 s of solo piano: 24 s on the CPU (AMD GPU, no CUDA), 1201 notes, C2 to A6.
- YouTube refuses the owner's connection for every video and yt-dlp client
  unless signed in. The owner confirmed `signin.py` signs in.
- mp7.dev has no public API; the owner's viner.dev sites are GitHub Pages and
  cannot run a downloader.
- A running `MIDIShell.exe` blocks the shell link step (LNK1168); ask the owner
  to close it before rebuilding.
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting any
  click or screenshot.
- Live captures need no cursor: pass a `.mid` as `argv[1]` to open it, and use
  `PrintWindow` with flag 3, since `CopyFromScreen` grabs whatever covers it.

## Work completed

- Local converter: Python 3.12.10, FFmpeg 9.0.1 and Deno 2.9.6 by winget; venv
  at `D:\Dev\mp3converter\.venv` (torch 2.14.0+cpu, transkun 2.0.1, yt-dlp
  2026.8.19, pywebview 6.2.1); `build\shell\converter\.venv` is a junction to it.
- Convert audio popup: choose a file or paste a link, status, cancel, Sign in
  to YouTube; the `.mid` lands in the MIDI folder and is rescanned.
- UI pass: state pills sized in semibold so they no longer shift; Open, Choose
  folder and Convert merged into one + menu; `RoundCorners` masks square rows
  and headers in the file list and track table.

## Unresolved

- **Owner to confirm `83fa9ed`.** Shadow bands now fade down the sides past
  each corner; Tracks gained outer padding and aligned MUTE/SOLO headings.
  The owner never named the Tracks fault, so these were inferred.
- **YouTube link inside the app after sign-in:** not yet confirmed to convert.
- **Release bundle:** `converter\` beside the exe with an embeddable Python,
  the packages including pywebview, both scripts and `ffmpeg\`. Size not weighed.
- **Panel seat, not started:** `PROMPT-S-CURVE-AND-SWITCHES.md`, a styling pass
  on the Convert popup, the owner's UI items in `SHELL-GAPS.md`.
- **Needs the owner at the keyboard:** delivery into a game, Wooting feel, two
  devices at once, live curve reconnection, a mixed-DPI move.

## Validation actually run

- At `83fa9ed`: `ui\MIDIShell.vcxproj` built; `tests\run-shell-tests.ps1
  -Render` passed, 0 failures. Live 125% captures before and after, compared
  by pixel dump of the file list corner.
- At `ad05987`: `tests\run-shell-parity-mutations.ps1`, 19 of 19 killed. Not
  rerun after the two UI commits, which changed drawing code only.
- Not run: `run-native-tests.ps1` and `run-latency-tests.ps1`, which take the
  cursor. Nothing was played into a game.

## Build and test

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' 'ui\MIDIShell.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
& .\tests\run-shell-tests.ps1 -Render
& .\tests\run-shell-parity-mutations.ps1
```

Shell tests capture injection in process, so always safe; never `*>&1` in PS 5.1.

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus; `upstream` is Zephkek/MIDIPlusPlus.
  `main` stays at `e37ba7e`. Pushing without asking is authorized.
- Pushed and clean; code last changed at `83fa9ed`; supersedes `-4d1c91`.
- Main checkout is on `output-panel`; `D:\Dev\mpp-panels` is on the old
  `astra/shell-parity` (`bdd7e85`).
- Never commit `x64/Release/midi/`, `build/`, `tools/mp3-to-midi/cookies.txt`
  or `tools/mp3-to-midi/browser/`.

## Next action

Ask the owner to open `build\shell\MIDIShell.exe` from this branch and confirm
the file list shadow and the Tracks spacing; if Tracks still looks wrong, ask
which gap they mean before changing `ui/Panels.cpp`.
