MIDI++ test build
=================

Turns a MIDI keyboard into keystrokes for virtual piano games, and plays MIDI
files the same way.


Running it
----------

Unzip, keep the files together, run MIDIShell.exe.

Nothing to install. Windows 10 or 11, 64-bit.

SmartScreen warns on first run because the build isn't signed. More info, then
Run anyway.


Playing something
-----------------

MIDI++ types into whatever window is focused, so click into the game first.

Live: plug your keyboard in, open the gear icon at the top right, pick your
device.

Files: drop .mid files in the midi folder next to the exe and hit Refresh. You
can also drag a file onto the window, or pick another folder with the folder
button. Sub-folders are included.

The keyboard icon opens Key Mapping. Click a note on the piano, then press the
key you want it to send.


What to try
-----------

- Test Kernel Streaming.
- Play a MIDI file into your game. Do the notes land in time?
- Play live. Does the velocity feel right? Velocity Response at the bottom
  controls that.
- Try the other transports in Settings if your hardware gives you more than one.
  Tell me if one is better.
- Look for anything cut off or too small to read.

If something breaks, send your Windows version, display scale, MIDI device, and
which transport you were on.


Known gaps
----------

- Some panels from the old version aren't back yet.
- Copy as sheet has no velocity or note lengths.


Licence
-------

GPLv3, forked from Zephkek/MIDIPlusPlus. See LICENSE. Ask me for the source if
you want it.


What's in here
--------------

  MIDIShell.exe   the app
  config.json     key mappings and settings
  midi\           put your .mid files here
  LICENSE         GPLv3
  *-LICENSE.txt   third-party notices

The app writes shell-settings.json next to itself for window size and theme.
Delete it to reset those.
