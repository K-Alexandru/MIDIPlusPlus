"""Audio to MIDI for MIDI++: one file or link in, one .mid out.

The app runs this beside itself, never in process (HANDOFF.md, the YouTube to
MIDI pipeline). Transcription is the Transkun model, the same one LioK251's
mp3converter wraps (MIT, see README.md in this folder); links go through
yt-dlp and FFmpeg first.

Every line on stdout is one status the app reads:

    step: <what is happening now>
    done: <path of the .mid written>
    error: <why it stopped>

Anything else, such as Transkun's own progress, is passed through as text.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile


def say(kind, text):
    print(f"{kind}: {text}", flush=True)


def is_link(value):
    return value.lower().startswith(("http://", "https://"))


def unique_path(folder, stem):
    candidate = os.path.join(folder, f"{stem}.mid")
    number = 2
    while os.path.exists(candidate):
        candidate = os.path.join(folder, f"{stem} ({number}).mid")
        number += 1
    return candidate


def clean_stem(name):
    stem = "".join("_" if c in '<>:"/\\|?*' or ord(c) < 32 else c for c in name).strip(" .")
    return stem[:120] or "conversion"


def download(link, folder):
    import yt_dlp

    say("step", "Downloading the audio")
    options = {
        "format": "bestaudio/best",
        "outtmpl": os.path.join(folder, "%(title)s.%(ext)s"),
        "noplaylist": True,
        "quiet": True,
        "no_warnings": True,
        "noprogress": True,
        "postprocessors": [{"key": "FFmpegExtractAudio", "preferredcodec": "mp3"}],
    }
    with yt_dlp.YoutubeDL(options) as ydl:
        info = ydl.extract_info(link, download=True)
        if info is None:
            raise RuntimeError("The link has no audio that can be downloaded.")
        audio = os.path.splitext(ydl.prepare_filename(info))[0] + ".mp3"
    if not os.path.exists(audio):
        raise RuntimeError("The download finished without an audio file.")
    return audio, info.get("title") or "conversion"


def device(choice):
    if choice != "auto":
        return choice
    import torch

    return "cuda" if torch.cuda.is_available() else "cpu"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("source", help="an audio file, or an http(s) link")
    parser.add_argument("--out-dir", required=True, help="the folder the .mid is written to")
    parser.add_argument("--device", default="auto", help="auto, cpu or cuda")
    args = parser.parse_args()

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if not os.path.isdir(args.out_dir):
        say("error", f"The output folder does not exist: {args.out_dir}")
        return 2
    if shutil.which("ffmpeg") is None:
        say("error", "FFmpeg is not installed, and Transkun needs it to read audio.")
        return 2

    work = tempfile.mkdtemp(prefix="midipp-convert-")
    try:
        if is_link(args.source):
            audio, title = download(args.source, work)
        else:
            if not os.path.isfile(args.source):
                say("error", f"The file does not exist: {args.source}")
                return 2
            audio, title = args.source, os.path.splitext(os.path.basename(args.source))[0]

        target = unique_path(args.out_dir, clean_stem(title))
        partial = os.path.join(work, "transcribed.mid")
        chosen = device(args.device)
        say("step", f"Transcribing on the {chosen.upper()}; a song takes a few minutes")
        # Transkun's progress goes to stderr, which the app shows as plain text.
        result = subprocess.run(
            [sys.executable, "-m", "transkun.transcribe", audio, partial, "--device", chosen],
            stdout=sys.stdout,
            stderr=subprocess.STDOUT,
        )
        if result.returncode != 0 or not os.path.exists(partial):
            say("error", f"Transkun stopped with exit code {result.returncode}.")
            return 1
        shutil.move(partial, target)
        say("done", target)
        return 0
    except Exception as failure:  # the app needs one line, not a traceback
        say("error", str(failure) or type(failure).__name__)
        return 1
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
