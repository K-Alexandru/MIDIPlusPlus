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

MIDI++ types into whichever window is focused, so click into the game first.

Live: plug your keyboard in, open the gear icon at the top right, pick your
device.

Files: drop .mid files in the midi folder next to the exe and hit Refresh. You
can also drag a file onto the window, or pick another folder with the folder
button. Sub-folders are included.

The keyboard icon opens Key Mapping. Click a note on the piano, then press the
key you want it to send.


What to try
-----------

- Right notes in your game, in time?
- Playing live, does the velocity feel right? Velocity Response at the bottom
  is what shapes it.
- Settings lists four MIDI transports. You'll only see the ones your hardware
  supports. If you get more than one, say whether either is better.
- Anything squashed, cut off, or unreadable on your monitor.

If something breaks, send your Windows version, display scale, MIDI device, and
which transport you were on.


Known gaps
----------

- Some panels from the old version aren't back yet.
- Kernel Streaming is new and unproven. If you can try it, say what happened.
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

shell-settings.json appears next to the exe for window size and theme. Delete
it to reset those.
