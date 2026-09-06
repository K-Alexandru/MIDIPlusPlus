<#
.SYNOPSIS
  Render every skin and window mode of skin-system.html to PNG in docs/design/.

.DESCRIPTION
  Committed screenshots so the spec can be looked at without a browser, a
  server or a running build, and so a change to the mockup shows up as a
  visible diff rather than a wall of HTML.

  Headless Edge is used because it is the Chromium already on this machine;
  there is no Node or Python here to drive Playwright. Edge itself is happy
  with file: URLs, so no server is needed for capture. Fonts come from Google
  Fonts, so the first run needs network; without it the captures fall back to
  Segoe UI and the metrics shift slightly.

.EXAMPLE
  .\tools\capture-mockup.ps1
  .\tools\capture-mockup.ps1 -Case modern-dark-full
#>
[CmdletBinding()]
param(
  [string]$Case = '*',
  [string]$OutDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'docs\design'),
  [switch]$KeepProfile
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$page = Join-Path $repo 'skin-system.html'
if (-not (Test-Path -LiteralPath $page)) { throw "skin-system.html not found at $page" }

$candidates = @(
  "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe",
  "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
  "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
  "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe"
)
$browser = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $browser) { throw "No Edge or Chrome found. Looked in:`n  " + ($candidates -join "`n  ") }

# Sizes are the mockup's own, from the sizes map in skin-system.html, plus the
# 24px desk padding on each side and slack for the drop shadow. Frame mode
# stacks from the top, so an oversized viewport adds background and a short one
# would crop; err high.
$cases = @(
  @{ name = 'classic-full';      skin = 'classic';      mode = 'full'; keys = 0; w = 1150; h = 700 }
  @{ name = 'classic-dark-full'; skin = 'classic-dark'; mode = 'full'; keys = 0; w = 1150; h = 700 }
  @{ name = 'modern-full';       skin = 'modern';       mode = 'full'; keys = 0; w = 1150; h = 795 }
  @{ name = 'modern-dark-full';  skin = 'modern-dark';  mode = 'full'; keys = 0; w = 1150; h = 795 }

  @{ name = 'classic-keymap';    skin = 'classic';      mode = 'full'; keys = 1; w = 900; h = 320; only = 'keys' }
  @{ name = 'modern-keymap';     skin = 'modern';       mode = 'full'; keys = 1; w = 900; h = 345; only = 'keys' }

  @{ name = 'classic-mini-live'; skin = 'classic';      mode = 'live'; keys = 0; w = 540; h = 230 }
  @{ name = 'modern-mini-live';  skin = 'modern';       mode = 'live'; keys = 0; w = 540; h = 310 }
  @{ name = 'classic-mini-auto'; skin = 'classic';      mode = 'auto'; keys = 0; w = 540; h = 330 }
  @{ name = 'modern-mini-auto';  skin = 'modern';       mode = 'auto'; keys = 0; w = 540; h = 375 }
)

$selected = $cases | Where-Object { $_.name -like $Case }
if (-not $selected) { throw "No case matches '$Case'. Available: " + (($cases | ForEach-Object { $_.name }) -join ', ') }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$profileDir = Join-Path ([System.IO.Path]::GetTempPath()) ("mockup-capture-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $profileDir | Out-Null

Write-Host "Browser: $browser"
Write-Host "Output:  $OutDir"
Write-Host ""

try {
  foreach ($c in $selected) {
    $query = "frame=1&skin=$($c.skin)&mode=$($c.mode)&keys=$($c.keys)"
    if ($c.only) { $query += "&only=$($c.only)" }
    $url = ([System.Uri]$page).AbsoluteUri + "?" + $query
    $out = Join-Path $OutDir ($c.name + '.png')

    # Not $args: that is an automatic variable inside a script.
    $edgeArgs = @(
      '--headless=new'
      '--disable-gpu'
      '--hide-scrollbars'
      '--force-device-scale-factor=1'
      '--no-first-run'
      '--no-default-browser-check'
      '--disable-extensions'
      "--user-data-dir=$profileDir"
      # Web fonts and the initial render need to settle before the shot; the
      # page has no animation to wait out beyond that.
      '--virtual-time-budget=4000'
      "--window-size=$($c.w),$($c.h)"
      "--screenshot=$out"
      $url
    )
    # Start-Process, not the call operator: Windows PowerShell 5.1 turns a native
    # program's stderr into error records and Edge writes its progress there, so
    # a plain call reports a failure on every successful capture.
    $err = Join-Path $profileDir "edge-stderr.txt"
    Start-Process -FilePath $browser -ArgumentList $edgeArgs -NoNewWindow -Wait `
      -RedirectStandardError $err -RedirectStandardOutput (Join-Path $profileDir "edge-stdout.txt")

    if (Test-Path -LiteralPath $out) {
      $kb = [math]::Round((Get-Item -LiteralPath $out).Length / 1KB)
      Write-Host ("  {0,-20} {1,4} x {2,-4}  {3} KB" -f $c.name, $c.w, $c.h, $kb)
    } else {
      Write-Warning "  $($c.name): no file written"
    }
  }
}
finally {
  if (-not $KeepProfile -and (Test-Path -LiteralPath $profileDir)) {
    Remove-Item -Recurse -Force -LiteralPath $profileDir -ErrorAction SilentlyContinue
  }
}

Write-Host ""
Write-Host "Done. Regenerate after any change to skin-system.html."
