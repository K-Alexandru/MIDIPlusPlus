MIDI++ test build
=================

Turns a MIDI keyboard into keystrokes so you can play virtual piano games, and
plays MIDI files the same way.

This build is rough in places. There's a list of what isn't done near the
bottom, have a look before you tell me something's broken.


Running it
----------

Unzip the folder, keep the files together, run MIDIShell.exe.

Nothing to install. No redistributables, no .NET, no drivers. Windows 10 or 11,
64-bit.

SmartScreen will complain the first time because the build isn't signed. Click
More info, then Run anyway.


Playing something
-----------------

MIDI++ types into whichever window is focused, so click into the game first. If
MIDI++ has focus, that's where the keys go.

Live playing: plug your keyboard in, hit the gear icon at the top right, pick
your device.

MIDI files: drop them in the midi folder next to the exe and hit Refresh. You
can also drag a file onto the window, or point it at any folder with the folder
button. Sub-folders work, so your whole library can go in there.

The keyboard icon at the top right opens Key Mapping. It shows which key each
note types and lets you change any of them: click a note on the piano, then
press the key you want it to send.


What I'd like you to try
------------------------

- Does it play the right notes in your game, in time?
- Playing live, does the velocity feel right? The Velocity Response panel at
  the bottom is what shapes that.
- Settings lists four MIDI transports: WinRT, WinMM, Kernel Streaming and
  Wooting Analog. You'll only see the ones your hardware supports. If you get
  more than one, tell me whether either feels better.
- Anything that looks squashed, cut off, or unreadable on your monitor.

If something goes wrong, tell me your Windows version, your display scale
(Settings, System, Display), what MIDI device you're on, and which transport
you picked. Saves me guessing.


What isn't done yet
-------------------

- The interface is a rewrite and some panels from the old version aren't back
  yet.
- Kernel Streaming is brand new. It finds devices fine, but nobody has
  confirmed notes actually coming through it. If you've got hardware, try it.
  Either way, that's the most useful thing you can tell me.
- Wooting analog keyboards work in the code but have never been played on a
  real one.
- You can open two MIDI devices at once. Nobody has played through both.
- Legit mode, meant to make autoplay sound less robotic, is off. It works, it
  just sounds wrong, so it's hidden.
- Copy as sheet gives you virtual piano text. No velocity, no note lengths.
  That notation doesn't carry them.

Autoplay has had the most work behind it. Live playing is where you're most
likely to find something odd.


Licence
-------

GPLv3, forked from Zephkek/MIDIPlusPlus. See LICENSE.

That means you can have the source for this exact build. Ask me, or use the
link that came with it.

ImGui-LICENSE.txt (MIT) and IBM-Plex-LICENSE.txt (SIL OFL 1.1) cover the two
bundled libraries. RtMidi is MIT and is covered in LICENSE.


What's in here
--------------

  MIDIShell.exe   the app
  config.json     your key mappings and settings, the app writes to it
  midi\           put your .mid files here
  LICENSE         GPLv3
  *-LICENSE.txt   the other two

It also drops a shell-settings.json next to itself for window size and theme.
Delete that if you want those reset; it won't touch anything else.
