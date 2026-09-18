# Start here

Updated 2026-09-18 on `claude/consolidate-2026-09-15`, the one branch. Read
`HANDOFF.md` only where this points, `SHELL-GAPS.md` (owed) and `SEATS.md`.

## Goal

Build the five items `SHELL-GAPS.md` lists under "Asked for on 2026-09-18", in
its order. Items 1 and 2, hotkeys and custom themes, are built (`9d0f64f`) and
not fully pressed. Item 3, Legit mode, is next: the owner wants it rethought
entirely, not tuned. QuartzMIDI is the ImGui shell (`ui/`) over `PlaybackCore`.
The panel seat owns `ui/` (`SEATS.md`); what the owner asks for there is in scope.

## Decisions made, do not reopen

- The name is QuartzMIDI everywhere a user sees it; MIDI++ survives only in
  source tree names and credits.
- Nothing sent out carries an account name, an assistant name or a working
  doc. Commits carry no `Co-Authored-By` line, in any repository.
- The public source goes to `greasebob/QuartzMIDI`: a new repository, one fresh
  commit of what builds the app, author and committer
  `greasebob <240342428+greasebob@users.noreply.github.com>`, the owner's yes
  on metadata, README and a name scan first. Parked by the owner on
  2026-09-18: fixes and features come before the push.
- UI copy is the owner's voice (`HANDOFF.md` section 15): no explanatory text,
  not even why a control is disabled; redesign the control. Names are plain
  and professional: "Auto-generate Dark", not "Dark follows this one".
- Hotkeys: `SHELL-GAPS.md` item 1. No media key is ever a default. The legend
  is a key and its action's icon in one outline at every width, never words,
  and never shows the seek seconds.
- Custom themes: `SHELL-GAPS.md` item 2 holds the design. A custom theme is
  colour only, like every skin. Swatches and the picker are drawn by the app,
  not ImGui's widgets.
- Legit Mode wears an Experimental tag in Settings until it is rethought.
- The game owns the key protocol: Alt is velocity, Ctrl is the 88-key notes,
  the velocity keys are the note keys. A fix or a test never changes a bind.
- Test builds stay on this PC: `make-release.ps1` zips to `build\release\`.

## Relevant files

- Legit mode: `LEGIT-MODE.md` (what it does, why at dispatch, its tests and the
  2026-09-04 verdict), `HANDOFF.md` line 352 (what a second attempt is worth),
  `MIDI++/PlaybackCore.cpp` (the batch loop near line 760, the `legit_*`
  functions near 1390), `LegitModeSettings` in `MIDI++/config.hpp`,
  `Action::LegitMode` in `ui/ShellEngine.cpp`, the switch in `ui/Panels.cpp`.
- Themes: `ui/ThemeModel.hpp`; `ThemeSwatch`, `DrawThemeEditor` and Appearance
  in `ui/Panels.cpp`. Hotkeys: `ui/HotkeyNames.hpp`, `ui/Shell.cpp`.
- `tests/ShellTests.cpp` captures injection in process; `tests/RenderTests.cpp`
  writes a PNG per scenario to `build\render-tests`.
- Run `tests\run-shell-tests.ps1 -Render`, then `tools\make-release.ps1` and
  `python tools\make-source.py`. Legit's own tests: `LatencyTests.exe --legit`.

## Verified facts

- Legit mode today is three memoryless uniform draws at dispatch: a late-only
  press offset of up to 5 ms at the default, a 2% dropped note-on, and on 5%
  of batches a 50 to 200 ms hesitation. The offsets and the hesitation are
  `sleep_for` on the dispatch thread itself, so everything due in that window
  waits behind them; a tester called it laggy.
- Preferences name a theme by id plus `dark`; `themes.json` sits beside
  `shell-settings.json` and the shell restyles on a signature of the colours.
- `ShellTests.exe` stops at its first failure and buffers output: run the exe
  itself to read what it printed.
- `gh` holds two accounts, K-Alexandru active. Credential Manager offers
  greasebob and `origin` refuses it; push with
  `git -c credential.helper= -c credential.helper="!gh auth git-credential"`.
- A running `QuartzMIDI.exe` blocks the shell link and `make-release.ps1`.
  Ask the owner to close theirs; never kill it.
- The display runs at 125%; read `tests/NativeShell.ps1` before scripting a
  click. PowerShell 5.1: never redirect a native exe with `2>&1` under
  `$ErrorActionPreference = 'Stop'`; run `run-shell-tests.ps1` bare. Count
  lines with `(Get-Content f).Count`; `Measure-Object -Line` skips blank ones.

## Work completed

- To 2026-09-17: `HANDOFF.md`; then `REVIEW-2026-09-18.md` and `LATENCY.md`.
- 2026-09-18, `b674608` to `4fafdb7`, from one tester: Wooting strike speed,
  the Roblox tab-out gate, velocity taps off held keys, a stable load sort,
  zero-length notes, one strike per key, AutoVol preselect, darker light skins.
- 2026-09-18, `3fc9e86` to `9d0f64f`: six rebindable hotkeys with media keys and
  an icon legend; custom themes with an editor and the app's own picker;
  Settings rows' hover fill widened; Legit Mode tagged Experimental;
  `make-source.py` drops `MIDI++.APS`.

## Unresolved

- **Hotkeys: the owner bound F5 and F6 in the app; the rest is unpressed.** The
  Sol seat or the owner: a media key, a key another program holds, move a key
  between actions, unbind, Escape out, hold the key past its repeat, and
  capture with Settings as its own window in mini mode.
- **Themes: the app's own picker has never been dragged.** Drag the field and
  the bars, an alpha bar under Fine detail, type a colour, the three starting
  colours, both halves, an unpaired theme, delete, and restart to see
  `themes.json` come back.
- **Owner to say** whether a note key may be a hotkey: bound, it is taken from
  the game while the app is open, and nothing stops it today.
- **Tester shampoojr** to confirm the tester list on the `9d0f64f` build; none
  of it has met a real Wooting or Roblox. His doubled note was never reproduced.
- **Owner to say** whether light mode is dark enough; then `skin-system.html`,
  which shows the old values and knows nothing of themes, takes the new ones.
- **Publishing**, parked, and the Colab notice behind it. `build\publish` holds a
  staged commit whose README and `.gitignore` are to keep; restage it first.
- **Tester on Visual Studio 2026** to build the source zip or send the error.
- **Owner to test**: velocity editor, WinMM, mini, sheets, MIDI out, mixed DPI.

## Validation actually run

- `8baf9bd`: `ShellTests.exe`, parity, 360 render scenarios, the native suite
  and `LatencyTests.exe --loopback` PASS.
- `b674608` to `4fafdb7`: `run-shell-tests.ps1 -Render` PASS at each commit; the
  tester fixes' tests fail with their fix reverted, AutoVol preselect has none.
- `319740e` to `9d0f64f`: `run-shell-tests.ps1 -Render` PASS at each, 460 render
  scenarios at the last. The hotkey, six-key, `settings-switches`,
  `theme-editor` and `theme-picker` renders were read at 125% in one or two
  skins each; no render shows an alpha bar. The hotkey restart test fails with
  the `validate` change reverted. Capture is tested against a key table, never
  `GetAsyncKeyState`. The source zip builds on both SDKs.
- Legit mode: nothing run this session; `LatencyTests.exe --legit` last ran
  before 2026-09-18.

## Repository state

- `origin` is K-Alexandru/MIDIPlusPlus, public; `upstream` is
  Zephkek/MIDIPlusPlus. `main` stays at `e37ba7e`. Pushing to `origin` without
  asking is authorized; creating the public repository is not.
- This branch is pushed and clean apart from the owner's
  `x64\Release\MIDI++.exe` and `x64\Release\midi\`; leave `D:\Dev\mpp-panels`.
- Demo v2 from `9d0f64f`: `build\release\QuartzMIDI-demo-v2.zip`, SHA256
  `4003221F4C72023E16169A72E58C7F2B877CE0F61EFBCFEE022F6E0D0775BA4D`, and
  `QuartzMIDI-source-9d0f64f.zip`; send no older pair.
- Never commit `x64/Release/midi/`, `build/`, `MIDI++/MIDI++/`, `.claude/`,
  `tools/mp3-to-midi/cookies.txt` or `tools/mp3-to-midi/browser/`.

## Next action

Read `LEGIT-MODE.md` and `HANDOFF.md` line 352, then write under `SHELL-GAPS.md`
item 3 what Legit mode is for and a redesign that starts from its verdict, and
show that to the owner before changing `MIDI++/PlaybackCore.cpp`.
