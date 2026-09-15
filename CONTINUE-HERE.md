# Start here

Updated 2026-09-15 on `claude/continue-here-f0c3de`. Then read `HANDOFF.md`,
`SHELL-GAPS.md` (what the shell owes) and `SEATS.md` (who does what).

## Goal

One Windows app that does everything the original MIDI++ window did and more.
The ImGui shell (`ui/`, `build\shell\MIDIShell.exe`) replaces the Win32 window
(`MIDI++/`, `x64\Release\MIDI++.exe`); both share `PlaybackCore` through
`ShellEngine`. Nothing the original had is optional (`SHELL-GAPS.md`).

## Seats

- Panel seat owns `ui/`; Claude owns `MIDI++/`, `tests/`, specs, engine seams.
- Owner-approved exception, not a precedent: Convert, sign-in, and the owner's
  UI fixes in `ui/Panels.cpp` and `ui/SkinDraw.cpp`.

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
- The Tracks table draws its own rules after the table, above each row, never
  under the last; the owner approved the result on 2026-09-15.

## Relevant files

- `tools/mp3-to-midi/`: `convert.py` (file or link to `.mid`, one status per
  line), `signin.py` (WebView2 sign-in, writes `cookies.txt`), `README.md`.
- `MIDI++/AudioToMidi.hpp`: finds the install, runs either script in a job;
  `AudioToMidiTests` in `tests/ShellTests.cpp` covers it.
- `ui/ShellEngine.cpp`: `ConvertAudio`, `ConvertCancel`, `ConvertProgress`, `YouTubeSignIn`.
- `ui/Panels.cpp`: + menu, Convert popup, file list, Tracks table (`rules`).
- `ui/SkinDraw.cpp`: `InnerShadow`, `RecessedRect`, `RoundCorners`.

## Verified facts

- Transkun and mp3converter are MIT; `convert.py` copies no code from either.
- 93 s of solo piano: 24 s on the CPU (AMD GPU, no CUDA), 1201 notes, C2 to A6.
- YouTube refuses the owner's connection for every video and yt-dlp client
  unless signed in. The owner confirmed `signin.py` signs in.
- mp7.dev has no public API; the owner's viner.dev sites are GitHub Pages and
  cannot run a downloader.
- A running `MIDIShell.exe` blocks the shell link step (LNK1168); ask the owner
  to close it before rebuilding. Check which worktree's exe they are running.
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting any
  click or screenshot.
- Live captures need no cursor: pass a `.mid` as `argv[1]` to open it, and use
  `PrintWindow` with flag 3 on the process you launched, filtered by path;
  `CopyFromScreen` grabs whatever covers the window.
- The owner's MIDI files each have one note track; build a multi-track `.mid`.
- Each worktree's `build\shell` needs its own `converter\.venv` junction to
  `D:\Dev\mp3converter\.venv`, and its own sign-in (`tools/mp3-to-midi/cookies.txt`).

## Work completed

- Local converter: Python 3.12.10, FFmpeg 9.0.1 and Deno 2.9.6 by winget; venv
  at `D:\Dev\mp3converter\.venv` (torch 2.14.0+cpu, transkun 2.0.1, yt-dlp
  2026.8.19, pywebview 6.2.1); `build\shell\converter\.venv` is a junction to it.
- Convert audio popup: choose a file or paste a link, status, cancel, Sign in
  to YouTube; the `.mid` lands in the MIDI folder and is rescanned.
- UI pass: stable state pills, one + menu, `RoundCorners` on file list and table.
- `83fa9ed`: inner shadow bands continue down the sides and fade out. Owner
  confirmed it looks fine.
- `83fa9ed`, `e85e62f`: Tracks outer padding, one heading baseline and ink,
  rules above rows only, exact header rule. Owner: "its good".

## Unresolved

- **YouTube link inside the app after sign-in:** not yet confirmed to convert.
- **Release bundle:** `converter\` beside the exe with an embeddable Python,
  the packages including pywebview, both scripts and `ffmpeg\`. Size not weighed.
- **Panel seat, not started:** `PROMPT-S-CURVE-AND-SWITCHES.md`, a styling pass
  on the Convert popup, the owner's UI items in `SHELL-GAPS.md`.
- **Needs the owner at the keyboard:** delivery into a game, Wooting feel, two
  devices at once, live curve reconnection, a mixed-DPI move.

## Validation actually run

- At `e85e62f`: `ui\MIDIShell.vcxproj` built; `tests\run-shell-tests.ps1 -Render`
  274 PASS, 0 FAIL; live 125% captures of empty, one-track and three-track tables.
- At `e85e62f`: `tests\run-shell-parity-mutations.ps1`, 19 of 19 killed,
  sources restored.
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
- `claude/continue-here-f0c3de` is pushed; code last changed at `e85e62f`.
- Main checkout is on `output-panel`; `D:\Dev\mpp-panels` is on the old
  `astra/shell-parity` (`bdd7e85`).
- Never commit `x64/Release/midi/`, `build/`, `tools/mp3-to-midi/cookies.txt`
  or `tools/mp3-to-midi/browser/`.

## Next action

Ask the owner to sign in to YouTube from the Convert audio popup, paste a link,
and confirm the `.mid` lands in the MIDI folder.
