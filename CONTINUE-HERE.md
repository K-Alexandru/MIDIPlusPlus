# Start here

Entry point for the next session. Updated 2026-09-14 on
`claude/continue-here-4d1c91`, which is `claude/curves-pro-drums` plus the MP3
to MIDI work. Then read `HANDOFF.md` for architecture and decisions,
`SHELL-GAPS.md` for what the shell still owes, and `SEATS.md` for who does what.

## Goal

One Windows app that does everything the original MIDI++ window did and more.
The ImGui shell (`ui/`, `build\shell\MIDIShell.exe`) replaces the Win32 window
(`MIDI++/`, `x64\Release\MIDI++.exe`); both share `PlaybackCore` through
`ShellEngine`. Nothing the original had is optional (`SHELL-GAPS.md`).

## Seats

- The panel seat, a second assistant in another app, owns `ui/`, including
  `ShellEngine::Action` and `EngineSnapshot`.
- Claude owns `MIDI++/`, `tests/`, specs, and engine-side seams in `ui/`.
- One recorded exception: on 2026-09-14 the owner let Claude add the three
  Convert actions, their snapshot fields and a minimal popup. It is not a
  precedent.
- Cursor-driving or DPI-sensitive checks go to the panel seat (`SEATS.md`).

## Decisions made, do not reopen

- Velocity stays on a modifier (`VELOCITY_MODIFIER`); the tap is four events
  in one `SendInput` call.
- Injection never shares a thread with the message loop (`HANDOFF.md` section 4).
- Legit mode applies at dispatch, never at parse time (`LEGIT-MODE.md`).
- Timing numbers stop at the keyboard hook; never call them end-to-end latency.
- A skin chooses colour only; every skin is 1090 x 635 collapsed.
- Six built-in velocity curves, the sixth being S-Curve (`VELOCITY-CURVES.md`).
- Drum detection only labels tracks; auto-transpose never types arrow keys.
- MP3 to MIDI runs as a Python sidecar, never in process, and ships bundled
  with a release (the owner's choice, 2026-09-14).
- UI copy follows `HANDOFF.md` section 15; attribution follows section 13.

## Work completed, 2026-09-14

- Python 3.12.10 and FFmpeg 9.0.1 installed by winget, user scope.
- LioK251's mp3converter cloned to `D:\Dev\mp3converter` for reference only,
  with a venv holding torch 2.14.0+cpu, transkun 2.0.1 and yt-dlp 2026.8.19.
- `tools/mp3-to-midi/convert.py`: file or link in, one status per line out.
- `MIDI++/AudioToMidi.hpp`: runs it on its own thread in a job object.
- Shell: the plus button beside Choose MIDI folder opens Convert audio.
- `build\shell\converter\.venv` is a junction to the venv above, so the local
  shell finds Python; `build/` is not committed.
- `AudioToMidiTests` caught a Cancel after Done reporting a false second
  final status, fixed in `Job::Pump`.

## Verified facts

- Transkun and mp3converter are both MIT; `convert.py` copies no code.
- 93 s of solo piano transcribed in 24 s on the CPU: 1201 notes, C2 to A6.
- Pure sine tones transcribe to zero notes, because Transkun is a piano model.
- The GPU is an AMD RX 7900 XTX, so CUDA PyTorch does not apply here.
- Wikimedia refuses yt-dlp's default client with a 403; YouTube is untried.
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting any
  click or screenshot.

## Unresolved

- **The owner's in-app test:** run `build\shell\MIDIShell.exe`, open Convert
  audio, try one file and one YouTube link.
- **Release bundle:** `converter\` beside the exe with an embeddable Python,
  the packages, `convert.py` and `ffmpeg\`. Size and a download-on-first-use
  alternative are not yet weighed.
- **Panel seat, not started:** `PROMPT-S-CURVE-AND-SWITCHES.md` (S-Curve
  rename, two Settings switches, the export menu), a styling pass on the
  Convert popup, and the owner's UI pass items in `SHELL-GAPS.md`.
- **Needs the owner at the keyboard:** delivery into a game from the shell,
  Wooting feel, two devices at once, live curve reconnection, a mixed-DPI move.

## Validation actually run, at this branch's head

- `tests\run-shell-tests.ps1 -Render`: every shell test and every render
  view at 100, 125, 150 and 200% passed.
- `tests\run-shell-parity-mutations.ps1`: see the commit that updates this
- `ui\MIDIShell.vcxproj` Release x64 built.
- Not run: `run-native-tests.ps1` and `run-latency-tests.ps1`, which take the
  cursor. Nothing was played into a game, and no one has clicked Convert yet.

## Build and test

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' 'ui\MIDIShell.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
& .\tests\run-shell-tests.ps1 -Render
& .\tests\run-shell-parity-mutations.ps1
```

Shell tests capture injection in process and are always safe. In PowerShell
5.1, do not call them with `*>&1`, because native stderr aborts the script.

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus; `upstream` is Zephkek/MIDIPlusPlus.
  `main` stays at `e37ba7e`. Pushing without asking is authorized.
- `claude/curves-pro-drums` = `887336d`; this branch builds on it and is pushed.
- Main checkout is on `output-panel`; `D:\Dev\mpp-panels` is on the old
  `astra/shell-parity` (`bdd7e85`).
- Never commit `x64/Release/midi/` (the owner's music) or `build/`.

## Next action

Ask the owner how the in-app Convert test went, then fix what it shows; if it
worked, scope the release bundle.
