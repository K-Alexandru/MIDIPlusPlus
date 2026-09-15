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
3. `.venv\Scripts\python.exe` in this folder.

The script itself is `converter\convert.py` beside the exe, or this folder for a
development build. FFmpeg is found on `PATH`, or in an `ffmpeg` folder beside
the script.

For a development build, the quickest setup is a junction from
`build\shell\converter\.venv` to an existing environment. The shell tests run
from `build\shell-tests`, which has no converter, so they stay unaffected.

## When YouTube says "confirm you're not a bot"

YouTube blocks some connections from downloading without a signed-in
session. When it does, every video fails, whatever the settings. Checked
2026-09-14 on the owner's connection with every yt-dlp client and Deno
installed. Hosted converters such as mp7.dev download from their own servers,
which is why they still work.

Two ways around it:

1. Export your YouTube cookies in Netscape format, for example with a
   "Get cookies.txt LOCALLY" browser extension, and save the file as
   `cookies.txt` in this folder. The app never reads a browser itself.
2. Download the audio another way and use Choose audio file.

A JavaScript runtime (Deno or Node) on `PATH` is also needed for YouTube.
`convert.py` finds one on its own.

## Test it by hand

```powershell
& $env:MIDIPP_CONVERTER_PYTHON tools\mp3-to-midi\convert.py song.mp3 --out-dir x64\Release\midi
```

Transcription on the CPU takes a few minutes per song. Expect rough results for
anything that is not solo piano.
