# Spec against build

Run 2026-09-05 at `bf89408`, whose `ui/` is `e0aae89` unchanged.

The spec side is `docs/design/`, rendered from `skin-system.html`. The build
side is the real ImGui panels through the shipped DX11 backend:
`build/render-tests/` from `tests/run-shell-tests.ps1 -Render`, plus the seven
panel states from the local `PanelChecks` harness, which needed one repair to
compile: `NtUserSendInputCall` was removed in `e678893`.

Every finding below was read off a rendered pair and then confirmed in
`ui/Panels.cpp`, so each one names the line that produces it. Line numbers are
against `e0aae89`.

Everything is in `ui/`, which Astra owns. Nothing here was changed.

## Confirmed defects

**1. Track rows do not share a baseline.** `Ellipsis` draws at
`ImGui::GetCursorScreenPos()` (`ui/Panels.cpp:156`), which ignores the
`CurrLineTextBaseOffset` that `AlignTextToFramePadding` just set, while
`ImGui::Text` honours it. In a track row that splits the columns in two: Track,
Instrument and Channel sit high, `#` and Notes sit low with the mute and solo
buttons, about 7px apart at 100%. Visible in every skin; worst in Modern, whose
rows are taller. Fix `Ellipsis` to offset by the current line's text base
offset and every caller is corrected at once.

**2. Sustain cutoff is the only unstyled slider.** `ui/Panels.cpp:462` calls
`ImGui::SliderFloat` raw with a `"%.0f"` format, so it renders as ImGui's
default box with the number centred inside it. Every other slider in the shell
goes through `Groove` (`ui/Panels.cpp:126`), which is the spec's thin rail with
a round handle and the value outside; Transpose at `ui/Panels.cpp:1050` is the
comparison sitting two panels above it. The spec draws sustain cutoff as a
rail.

**3. Mini transpose spends two letters on buttons.** `ui/Panels.cpp:813` uses
`ImGui::InputInt`, whose built-in steppers are the characters `-` and `+`. The
house rule in `HANDOFF.md` section 15 is icons for utility controls, never
single letters, and the full window's speed stepper already does it correctly
with `Icon::Minus` and `Icon::Plus`. The spec's mini shows the transpose value
alone, with no stepper at all.

## Not ported, and it shows

The handoff's own note that "other panels were not restyled" is where all of
these come from. Listing them so the size of the remaining work is visible.

**4. The strip toggles are checkboxes.** `ui/Panels.cpp:799-804` draws
`ImGui::Checkbox` with the label outside, plus `TextDisabled("88 keys")` for
the key count. The spec has five pills with the label inside, tinted when on
and plain when off: Midi2Key, Velocity, Sustain, 88 Keys, MidiConnect. The
build has four controls and no MidiConnect, and the 88-key indicator reads as
disabled rather than as a state. This is the largest single visual difference
between the two, and it is the first thing in the window.

**5. The device is bare text.** "No MIDI input" where the spec has a bordered
pill holding the device name.

**6. The file list header is icons where the spec has words.** `ui/Panels.cpp:927-942`
gives "MIDI files" (spec: "MIDI Files"), three icon buttons for open, folder
and refresh, and a search field. The spec has a "Name" sort control and a text
"Refresh", no search field, and a right-aligned size per row. The build's rows
carry the name only.

**7. Selected file row has no accent bar.** The spec marks selection with a
tinted row and an accent bar down its left edge. The build tints only. Colour
alone is carrying the state, which the house rules rule out.

**8. Table headers are title case.** `ui/Panels.cpp:1085-1091` sets `#`,
`Track`, `Instrument`, `Ch`, `Notes`, `Mute`, `Solo`. The spec's header band is
uppercase meta type, and it spans MUTE and SOLO as one label over the two
columns.

**9. Status bar separators are pipe characters.** `ui/Panels.cpp:760-767`
concatenates `" | "` and ends with `"tracks silent | 88-key"`. The spec draws
spaced hairline rules between fields and a middle dot before the key count.
"Events/note" has no equivalent in the build.

**10. Mini's Live/Autoplay is a radio pair.** The spec has a two-cell segmented
control, the same shape as the mockup's own skin switcher. The build also drops
the "Curve" label the spec puts before the curve combo.

**11. Settings section labels are body text.** The spec has uppercase meta
labels for MIDI INPUT and BEHAVIOUR. The build's behaviour switches are bare
checkboxes with no description line, and two of the spec's three are absent:
Velocity hotkeys and Always on top. Only Solo piano tracks on load exists.

**12. Key mapping black-key labels are white.** The spec sets them in amber
against the ebony fill. The build uses the same ink as the white keys. Its
range reads `C2-C6` with a hyphen where the spec has an en dash.

## Deliberate, verified, leave alone

- No previous/next-file transport buttons. Recorded as inert in the mockup.
- No per-backend latency figures and no buffer controls in Settings. There is
  no Kernel Streaming backend and the collector starts after transport, so the
  build says "unavailable" rather than inventing numbers. This is the correct
  call.
- "tracks silent" instead of the spec's "tracks muted". The count is computed
  from solo state as well as mute, so silent is the accurate word.
- Track numbers start above 1 because original indices are preserved and
  note-free conductor tracks are hidden.
- Key mapping default black-key characters differ from the mockup's. The build
  reads the real `KEY_MAPPINGS`; the mockup's defaults were invented.
- The build's -10s and +10s carry chevron icons the spec draws as bare text.
  Minor, and arguably an improvement. Noted, not filed.

## Not compared

The render harnesses draw the panels without a native window, so the title bar,
the window buttons and the key mapping window's separate-window behaviour are
outside this pass. The native host was verified separately in the previous
session.

The velocity editor now has both sides: `mode-1` through `mode-3` on the build,
`classic-velocity*.png` and `modern-velocity*.png` on the spec. They were not
read against each other in this pass.

One thing the spec side settles: its histogram is labelled "Your playing this
session". `MIDI++/VelocityTelemetry.hpp` now records exactly that, and the
build can draw it as soon as the panel owner carries the field into
`EngineSnapshot`. CONTINUE-HERE.md has the field and what to draw with it.

## Reproducing

```powershell
& .\tools\capture-mockup.ps1          # spec side, into docs/design/
& .\tests\run-shell-tests.ps1 -Render # build side, into build/render-tests/
& .\tools\serve-mockup.ps1            # the live mockup, for states behind a click
```

The seven-state panel harness is a local artifact under ignored
`build/panels-qa/`, copied from the previous session's `build/shell-panels-qa/`
and repointed at this worktree. `mode-0` collapsed, `mode-1` to `mode-3`
velocity expanded, `mode-4` mini live, `mode-5` mini autoplay, `mode-6`
Settings, each at four skins and 100/150/200%.
