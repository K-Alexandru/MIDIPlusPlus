# Looking at the spec

`skin-system.html` at the repository root is the UI spec. It is an operable
mockup, not a picture: the toggles, track mutes, transport, settings, theme
switch and key mapper all work, and its measurements were taken from the
rendered page. Build against it rather than guessing.

There are three ways in, in increasing order of effort.

## 1. The screenshots in this folder

Committed, so they need no browser, no server and no build. Regenerate them
after any change to the mockup:

```powershell
& .\tools\capture-mockup.ps1
```

| File | Window |
| --- | --- |
| `classic-full.png` | Full window, Classic, 1090 x 635 |
| `classic-dark-full.png` | Full window, Classic Dark |
| `modern-full.png` | Full window, Modern, 1090 x 728 |
| `modern-dark-full.png` | Full window, Modern Dark |
| `classic-velocity.png` | Velocity editor expanded, Classic |
| `modern-velocity.png` | Velocity editor expanded, Modern |
| `classic-velocity-advanced.png` | Velocity editor with the 32-step editor open, Classic |
| `modern-velocity-advanced.png` | Velocity editor with the 32-step editor open, Modern |
| `classic-keymap.png` | Key mapping window, Classic, 840 x 246 |
| `modern-keymap.png` | Key mapping window, Modern, 840 x 268 |
| `classic-mini-live.png` | Mini, live input, Classic, 480 x 166 |
| `modern-mini-live.png` | Mini, live input, Modern, 480 x 243 |
| `classic-mini-auto.png` | Mini, autoplay, Classic, 480 x 264 |
| `modern-mini-auto.png` | Mini, autoplay, Modern, 480 x 305 |

The four full-window shots have the velocity editor collapsed; the four velocity
shots are the same window with it open. The settings panel and the transport
chooser are still behind clicks, so they need one of the two options below.

## 2. The mockup in a browser

Assistant browser tooling refuses local `file:` URLs, which is what blocked the
rendered comparison recorded in `CONTINUE-HERE.md`. Serve it over loopback
instead:

```powershell
& .\tools\serve-mockup.ps1
```

Then open `http://127.0.0.1:8756/skin-system.html`. The page takes deep links,
so a state can be pointed at rather than described:

| Parameter | Values |
| --- | --- |
| `skin` | `classic`, `classic-dark`, `modern`, `modern-dark` |
| `mode` | `full`, `live`, `auto` |
| `keys` | `1` shows the key mapping window, `0` hides it. Full mode only |
| `frame` | `1` hides the page prose, leaving the window alone |
| `only` | `keys` shows the key mapping window without the main one |
| `velocity` | `open` expands the velocity editor, `advanced` also opens the 32-step editor |

`http://127.0.0.1:8756/skin-system.html?skin=modern-dark&mode=live` opens on
that case directly. The same parameters work on a `file:` URL for anything that
can open one, which is how the capture script drives it.

## 3. The source

Reading the source is the only way to get exact geometry, and it is the check
that was skipped when the piano icon's black keys were invented rather than
copied. The icon set is a block of `<symbol>` elements; find it with:

```bash
grep -n '<symbol id=' skin-system.html
```

Each is a 24 x 24 viewBox. Most are stroke-only, sharing the sprite's stroke
width and round caps, and a filled shape in an icon the spec draws as strokes
will merge into a blob at 16px. Copy the path data rather than redrawing from
the rendered picture.

The rest of the file is worth knowing by shape: CSS custom properties and the
four skins are at the top, the window markup follows the icon sprite, and the
behaviour is one IIFE at the end. The sections after the live comparison carry
the sizing and operability notes.

## The comparison

`spec-vs-build.md` is the panel-by-panel comparison against the real shell,
with the line in `ui/Panels.cpp` that produces each difference.

## What these are not

They are pictures of the spec, not of the build. The shell's own render tests
write PNGs of the real ImGui window to the ignored `build/render-tests/`:

```powershell
& .\tests\run-shell-tests.ps1 -Render
```

Comparing the two folders is the comparison worth making.
