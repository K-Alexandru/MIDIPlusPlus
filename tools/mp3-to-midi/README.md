# Audio to MIDI

`convert.py` turns an audio file or a link into a `.mid` in the MIDI folder.
The app runs it as a separate process through `MIDI++/AudioToMidi.hpp`.

## Credit

The approach, Transkun behind yt-dlp and FFmpeg, is LioK251's
[mp3converter](https://github.com/LioK251/mp3converter), MIT,
"Copyright (c) 2025-2026 LioK". `convert.py` is written for this app and copies
no code from it. Transkun is by Yujia Yan, [MIT](https://github.com/Yujia-Yan/Transkun).

## What it needs

- Python 3.10 or newer, with `torch`, `transkun` and `yt-dlp[default]`.
- FFmpeg on `PATH`.

## Where the app looks for it

1. `MIDIPP_CONVERTER_PYTHON`, the full path of a `python.exe` that has the
   packages above.
2. `converter\python\python.exe` or `converter\.venv\Scripts\python.exe` beside
   the app's exe, which is how a release bundles it.

The script itself is `converter\convert.py` beside the exe, or this folder for a
development build.

## Test it by hand

```powershell
& $env:MIDIPP_CONVERTER_PYTHON tools\mp3-to-midi\convert.py song.mp3 --out-dir x64\Release\midi
```

Transcription on the CPU takes a few minutes per song. Expect rough results for
anything that is not solo piano.
