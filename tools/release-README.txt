MIDI++ — test build
===================

A Windows MIDI-to-QWERTY app for virtual piano games. It turns a MIDI keyboard
into keystrokes, and plays MIDI files as keystrokes.

This is a test build, not a finished release. Read "What is not finished" below
before you report anything.


Running it
----------

1. Unzip the whole folder somewhere. Keep the files together.
2. Run MIDIShell.exe.

Nothing to install. No Visual C++ redistributable, no .NET, no drivers. Windows
10 or 11, 64-bit.

Windows SmartScreen will warn you the first time, because the build is not code
signed. More info -> Run anyway.


Getting sound out of it
-----------------------

Open the game (or any text field, to see what it types) and give it focus.
MIDI++ types into whatever window is focused, so it must not be focused itself
while you play.

  Live playing   Plug in a MIDI keyboard, open Settings (gear, top right) and
                 pick your device. Play.
  MIDI files     Put .mid files in the midi folder beside the exe and press
                 Refresh, or drag a file onto the window, or use the folder
                 button.

Key Mapping (the keyboard icon, top right) shows which computer key each note
types, and lets you change any of them. Click a key on the piano, then press
the key you want it to send.


What to try, and what I want to hear about
------------------------------------------

- Does it type the right notes into your game, at the right time?
- Live playing: does velocity feel right? The Velocity Response panel at the
  bottom shapes how hard-you-press maps to what gets sent.
- The four MIDI transports in Settings: WinRT, WinMM, Kernel Streaming, Wooting
  Analog. Only some will be available depending on your hardware. If more than
  one works for you, is one noticeably better?
- Anything that looks wrong, cut off, or unreadable at your display scale.

Worth saying in a report: your Windows version, display scale (Settings ->
System -> Display -> Scale), your MIDI device, and which transport you picked.


What is not finished
--------------------

Said plainly so you do not spend time on known gaps:

- The UI is a rewrite in progress. Some panels from the older build are not
  ported yet.
- Kernel Streaming is new. It finds MIDI devices correctly, but actually
  playing through it has not been confirmed on any machine yet. If you try it,
  that is genuinely useful to hear about either way.
- Wooting analog keyboards are supported in code and have not been played on
  real hardware.
- Two MIDI devices can be opened at once. Playing through both at once has not
  been tried.
- "Legit mode", which humanises autoplay timing, is off. It exists but it
  sounds wrong, so it is not exposed.
- Sheet export ("Copy as sheet") produces virtual piano text. It has no
  velocity and no note length, because that notation does not carry them.

Autoplay accuracy is the part with the most work behind it. Live playing is the
part most likely to surprise you.


Licence
-------

GPLv3. See LICENSE. This is a fork of Zephkek/MIDIPlusPlus.

Because it is GPLv3, you are entitled to the source for this exact build. Ask
and you will get it — a link comes with this build if it did not reach you
already.

Third-party notices: ImGui-LICENSE.txt (MIT), IBM-Plex-LICENSE.txt (SIL OFL
1.1). RtMidi is MIT and is covered in LICENSE.


Files here
----------

  MIDIShell.exe   the app
  config.json     key mappings and settings; edited by the app, safe to keep
  midi\           put your .mid files here
  LICENSE         GPLv3
  *-LICENSE.txt   third-party notices

The app writes shell-settings.json beside itself for window and theme choices.
Deleting it resets those and nothing else.
