# Audio to MIDI

`convert.py` turns an audio file or a link into a `.mid` in the MIDI folder.
The app runs it as a separate process through `MIDI++/AudioToMidi.hpp`.

## Credit

The approach, Transkun behind yt-dlp and FFmpeg, is LioK251's
[mp3converter](https://github.com/LioK251/mp3converter), MIT,
"Copyright (c) 2025-2026 LioK". `convert.py` is written for this app and copies
no code from it. Transkun is by Yujia Yan, [MIT](https://github.com/Yujia-Yan/Transkun).

## What it needs

- Python 3.12 with the packages in `requirements.txt`; `pywebview` is for the
  sign-in window.
- FFmpeg and Deno on `PATH`, or in `ffmpeg\` and `deno\` beside the script.

## Where the app looks for it

1. `MIDIPP_CONVERTER_PYTHON`, the full path of a `python.exe` that has the
   packages above.
2. `converter\python\python.exe` or `converter\.venv\Scripts\python.exe` beside
   the app's exe, which is how a release bundles it.
3. `.venv\Scripts\python.exe` in this folder.

The script itself is `converter\convert.py` beside the exe, or this folder for a
development build. FFmpeg and Deno are found on `PATH`, or in `ffmpeg\` and
`deno\` beside the script.

For a development build, the quickest setup is a junction from
`build\shell\converter\.venv` to an existing environment. The shell tests run
from `build\shell-tests`, which has no converter, so they stay unaffected.

## The release bundle

`tools\make-release.ps1` stages `converter\` beside `MIDIShell.exe`: an
embeddable Python 3.12 in `python\` with the packages from `requirements.txt`
installed with `--no-deps`, this folder's two scripts, `ffmpeg\ffmpeg.exe`,
`deno\deno.exe`, and every licence under `licenses\`. Each download is
pinned by URL and SHA256 in the script.

`requirements.txt` lists what a real conversion, a link download and the
sign-in window load, recorded from `sys.modules` on 2026-09-15, not what
Transkun declares. Transkun's training and evaluation dependencies
(`matplotlib`, `seaborn`, `pandas`, `tensorboard`, `ncls`, `sox`,
`torch-optimizer`) are never imported by a transcription and are left out;
`ncls` has no wheel for Python 3.12 in any case. When a package is bumped,
check the list the same way: run `python -m transkun.transcribe` with a
`sitecustomize.py` on `PYTHONPATH` that dumps `sys.modules` at exit.

The script then runs the staged bundle with `PATH` cut to Windows alone and
converts a clip the bundled FFmpeg makes, so a tester with no Python gets the
same check.

## When YouTube says "confirm you're not a bot"

YouTube blocks some connections from downloading without a signed-in
session. When it does, every video fails, whatever the settings. Checked
2026-09-14 on the owner's connection with every yt-dlp client and Deno
installed. Hosted converters such as mp7.dev download from their own servers,
which is why they still work.

Ways around it:

1. Sign in to YouTube in the Convert audio popup. It runs `signin.py`, a
   small WebView2 window (pywebview) with its own profile in `browser\`. You
   sign in yourself; once YouTube shows the account, the session is saved as
   `cookies.txt` here. Confirmed working by the owner on 2026-09-14.
2. Export your YouTube cookies in Netscape format, for example with a
   "Get cookies.txt LOCALLY" browser extension, and save the file as
   `cookies.txt` in this folder. The app never reads a browser itself.
3. Download the audio another way and use Choose audio file.

yt-dlp warns that downloading while signed in can get an account flagged, so
a spare Google account is the safer choice. `signin.py` needs `pywebview`.

A JavaScript runtime (Deno or Node) is also needed for YouTube. `convert.py`
finds one on `PATH`, or Deno in `deno\` beside it.

## Playlists

With `--playlist`, a link inside a playlist converts every video in it, one
at a time, into the same folder. Each file is reported with a `saved:` line as
it lands; a video that fails is skipped and named in the log; one `finished:`
line ends the run. The app passes `--playlist` when Whole playlist is ticked.
Cancel stops the run and keeps the files already saved.

## Test it by hand

```powershell
& $env:MIDIPP_CONVERTER_PYTHON tools\mp3-to-midi\convert.py song.mp3 --out-dir x64\Release\midi
```

Transcription on the CPU takes a few minutes per song. Expect rough results for
anything that is not solo piano.
