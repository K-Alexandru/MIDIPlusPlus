"""Sign in to YouTube once, so convert.py can download links.

YouTube refuses some connections as bots unless the request is signed in.
This opens a small browser window (WebView2, through pywebview) with its own
saved profile. The user signs in themselves; nothing here sees or types a
password. When YouTube shows the account as signed in, the session is written
to cookies.txt beside this script, which convert.py reads. The profile is kept
in a "browser" folder beside this script, so the next run is already signed in.

Status lines on stdout, like convert.py:

    step: <what is happening now>
    done: <path of cookies.txt>
    error: <why it stopped>
"""

import email.utils
import os
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
COOKIES = os.path.join(HERE, "cookies.txt")
PROFILE = os.path.join(HERE, "browser")
START = "https://accounts.google.com/ServiceLogin?service=youtube&continue=https%3A%2F%2Fwww.youtube.com%2F"
# Present only once an account is signed in to YouTube.
SIGNED_IN = {"LOGIN_INFO", "SID", "__Secure-3PSID"}


def say(kind, text):
    print(f"{kind}: {text}", flush=True)


def expiry(morsel):
    stamp = morsel["expires"]
    if not stamp:
        return 0
    try:
        return int(email.utils.parsedate_to_datetime(stamp).timestamp())
    except (TypeError, ValueError):
        return 0


def write_cookies(jars):
    lines = ["# Netscape HTTP Cookie File", "# Written by signin.py for yt-dlp. Do not share: it is a signed-in session.", ""]
    names = set()
    for jar in jars:
        for name, morsel in jar.items():
            domain = morsel["domain"] or ".youtube.com"
            names.add(name)
            lines.append("\t".join([
                domain,
                "TRUE" if domain.startswith(".") else "FALSE",
                morsel["path"] or "/",
                "TRUE" if morsel["secure"] else "FALSE",
                str(expiry(morsel)),
                name,
                morsel.value,
            ]))
    temporary = COOKIES + ".partial"
    with open(temporary, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    os.replace(temporary, COOKIES)
    return names


def watch(window, finished):
    # Check every two seconds; the user may take a while to sign in.
    while not finished.is_set():
        time.sleep(2)
        try:
            url = window.get_current_url() or ""
            if "youtube.com" not in url:
                continue
            jars = window.get_cookies()
        except Exception:
            continue  # the window is closing or still loading
        names = {name for jar in jars for name in jar.keys()}
        if names & SIGNED_IN:
            write_cookies(jars)
            say("done", COOKIES)
            finished.set()
            window.destroy()
            return


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    try:
        import webview
    except ImportError:
        say("error", "pywebview is not installed for this Python.")
        return 2

    os.makedirs(PROFILE, exist_ok=True)
    say("step", "Sign in to YouTube in the window that opened")
    window = webview.create_window("Sign in to YouTube for MIDI++", START, width=520, height=720)
    finished = threading.Event()
    webview.start(watch, (window, finished), private_mode=False, storage_path=PROFILE)
    if not finished.is_set():
        say("error", "The window was closed before YouTube showed a signed-in account.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
