MIDI++ test build
=================

Turns a MIDI keyboard into keystrokes for virtual piano games, and plays MIDI
files the same way.


Running it
----------

Unzip, keep the files together, run MIDIShell.exe.

Windows 10 or 11, 64-bit.

SmartScreen warns on first run because the build isn't signed, so click More
info then Run anyway.

Delete shell-settings.json next to the exe to reset window size and theme.


Playing something
-----------------

MIDI++ types into whatever window is focused, so click into the game first.

Live: plug your keyboard in, open the gear icon at the top right, pick your
device.

Files: drop .mid files in the midi folder next to the exe and hit Refresh, drag
a file onto the window, or pick another folder with the folder button.
Sub-folders are included.

The keyboard icon opens Key Mapping, where you click a note on the piano then
press the key you want it to send.


What to try
-----------

- Test Kernel Streaming.
- Play a MIDI file into your game and check the notes land in time.
- Play live and tell me if the velocity feels right.
- Try the other transports in Settings if you have more than one.
- Look for anything cut off or too small to read.

If something breaks, send your Windows version, display scale, MIDI device, and
which transport you were on.


Known gaps
----------

- Some panels from the old version aren't back yet.
- Copy as sheet has no velocity or note lengths.


Licence
-------

GPLv3, forked from Zephkek/MIDIPlusPlus. Ask me for the source.
