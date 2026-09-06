# Native window checks for the ImGui shell.
#
# What this covers that `RenderTests.cpp` cannot. That harness drives ImGui IO
# and DX11 in process, so it never sends a window message and never moves a real
# cursor. Everything below is Windows' side of the app:
#
#   WM_GETMINMAXINFO   the minimum size the window actually enforces
#   WM_SIZE            ResizeBuffers and the render target rebuild
#   WM_DPICHANGED      surviving a move between monitors at different scales
#   WM_CLOSE           settings saved and the engine worker joined on the way out
#   the message pump   a physical click at a physical pixel hitting the control
#                      drawn there, at the display's real scale
#
# Run it from anywhere; it builds the shell first and works in its own directory,
# so it never touches the settings, config or MIDI folder in use.
#
#   & .\tests\run-native-tests.ps1
#   & .\tests\run-native-tests.ps1 -KeepCaptures    # leave the PNGs behind
#
# It drives the real cursor and brings a window to the front, so it takes over
# the desktop for about half a minute. Nothing else should be clicked meanwhile.

param([switch] $KeepCaptures, [switch] $SkipBuild)

$ErrorActionPreference = 'Stop'
$repoPath = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'NativeShell.ps1')

$script:failures = 0
function Check {
    param([Parameter(Mandatory)][bool] $Ok, [Parameter(Mandatory)][string] $Message, [string] $Detail)
    if ($Ok) { Write-Host "PASS $Message" }
    else {
        $script:failures++
        Write-Host "FAIL $Message"
        if ($Detail) { Write-Host "     $Detail" }
    }
}

# A minimal format 0 score: program change, then C4 and E4 a beat apart. Written
# here rather than committed as a binary so the fixture is readable and cannot
# drift from what the test claims it contains.
function Write-MinimalMidi {
    param([Parameter(Mandatory)][string] $Path)
    $bytes = [byte[]] (
        0x4D,0x54,0x68,0x64, 0x00,0x00,0x00,0x06, 0x00,0x00, 0x00,0x01, 0x00,0x60,   # MThd, format 0, 1 track, 96 tpqn
        0x4D,0x54,0x72,0x6B, 0x00,0x00,0x00,0x17,                                     # MTrk, 23 bytes
        0x00,0xC0,0x00,                                                               # program 0, acoustic grand
        0x00,0x90,0x3C,0x64,  0x60,0x80,0x3C,0x00,                                    # C4 on, off a beat later
        0x00,0x90,0x40,0x64,  0x60,0x80,0x40,0x00,                                    # E4 on, off a beat later
        0x00,0xFF,0x2F,0x00)                                                          # end of track
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

# Client-logical coordinates, multiplied by the window's scale at use.
#
# All four are anchored to the top-left corner, which is why they do not depend
# on the window size. The Files panel is `min(336, width * .33)` wide starting at
# the 12px window pad, so x = 150 is inside it at every size the window allows,
# down to the 900px minimum. The rows below it are the 81px Classic strip, the
# 12px pad, the panel pad, a 28px heading, a 28px search field and 8px item
# spacing, which puts the first file row's middle at y = 191 and the second 36
# below it.
#
# If the layout moves, the click checks below fail with "the file row is no
# longer here", not with something mysterious. Re-derive the numbers and update
# this block; do not chase the symptom.
$Layout = @{
    FileRow1   = @{ X = 150; Y = 191 }
    FileRow2   = @{ X = 150; Y = 227 }
    # A rectangle inside the Tracks panel that holds the track rows. Empty
    # before a file is loaded, so a change here means the load reached the UI.
    TrackRows  = @{ X = 370; Y = 300; W = 490; H = 120 }
}

if (-not $SkipBuild) {
    $msbuildPath = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
    & $msbuildPath (Join-Path $repoPath 'ui\MIDIShell.vcxproj') /p:Configuration=Release /p:Platform=x64 /m /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw 'Shell build failed.' }
}

$captureDirectory = Join-Path $repoPath 'build\native-tests\captures'
# Outside build\native-tests, which Start-NativeShell wipes to give the run a
# clean working directory.
$fixtureDirectory = Join-Path $repoPath 'build\native-fixture'
New-Item -ItemType Directory -Path $fixtureDirectory -Force | Out-Null
$fixture = Join-Path $fixtureDirectory 'native-fixture.mid'
Write-MinimalMidi -Path $fixture

$shell = Start-NativeShell -RepoPath $repoPath -MidiFiles @($fixture)
try {
    New-Item -ItemType Directory -Path $captureDirectory -Force | Out-Null

    # The main window, not a viewport. Get-Process.MainWindowHandle returns
    # whichever top-level window Windows prefers, and with viewports enabled that
    # can be the Key Mapping window; everything measured after that would be
    # measured against the wrong rectangle.
    $windows = @(Get-NativeShellWindows -ProcessId $shell.Process.Id)
    $named = @($windows | Where-Object { $_.Visible -and $_.Title -eq 'MIDI++ shell (ImGui)' })
    Check -Ok ($named.Count -eq 1) -Message 'exactly one visible window carries the shell title' `
          -Detail ("saw: " + (($windows | ForEach-Object { "'$($_.Title)' visible=$($_.Visible)" }) -join ', '))

    $geometry = Get-NativeShellGeometry -Shell $shell
    Check -Ok ($geometry.Dpi -ge 96) -Message "the window reports its DPI ($($geometry.Dpi), scale $($geometry.Scale))"

    # WM_GETMINMAXINFO. The handler builds the minimum from ImVec2(900, 580) and
    # AdjustWindowRectExForDpi, so the floor is in logical pixels and scales.
    Set-NativeShellRect -Shell $shell -X 60 -Y 60 -Width 300 -Height 300 -Topmost
    $clamped = Get-NativeShellGeometry -Shell $shell
    $minimumWidth = [int](900 * $clamped.Scale)
    $minimumHeight = [int](580 * $clamped.Scale)
    Check -Ok ($clamped.ClientWidth -ge $minimumWidth - 4 -and $clamped.ClientHeight -ge $minimumHeight - 4) `
          -Message 'a resize below the minimum is clamped, not obeyed' `
          -Detail "asked for 300x300, client is $($clamped.ClientWidth)x$($clamped.ClientHeight), floor is ${minimumWidth}x${minimumHeight}"

    # WM_SIZE, ResizeBuffers and the render target rebuild. A swap chain that
    # fails to resize renders a stale or blank frame rather than crashing, so
    # both are asserted: the frame differs between sizes, and it is never one
    # flat colour.
    $signatures = @()
    $index = 0
    foreach ($size in @(@(1400, 900), @(1180, 760), @(1700, 1040))) {
        Set-NativeShellRect -Shell $shell -X 60 -Y 60 -Width $size[0] -Height $size[1] -Topmost
        Check -Ok (-not $shell.Process.HasExited) -Message "the shell survives a resize to $($size[0])x$($size[1])"
        $capture = Get-NativeShellCapture -Shell $shell -Path (Join-Path $captureDirectory "resize-$index.png")
        try {
            $colours = @()
            foreach ($point in @(@(0.1, 0.1), @(0.5, 0.5), @(0.9, 0.4), @(0.5, 0.95))) {
                $colours += $capture.GetPixel([int]($capture.Width * $point[0]), [int]($capture.Height * $point[1])).ToArgb()
            }
            Check -Ok ((($colours | Sort-Object -Unique).Count) -gt 1) `
                  -Message "the frame at $($size[0])x$($size[1]) is drawn, not one flat colour"
            $signatures += Get-NativeShellRegionSignature -Bitmap $capture -X 0 -Y 0 -Width $capture.Width -Height $capture.Height
        } finally { $capture.Dispose() }
        $index++
    }
    Check -Ok ((($signatures | Sort-Object -Unique).Count) -eq $signatures.Count) `
          -Message 'each size renders its own frame rather than a stale one'

    # A physical click at a physical pixel. This is the check that only exists
    # natively: it goes through SetCursorPos, the message pump, the Win32
    # backend's mouse position and ImGui hit-testing, at the display's real
    # scale. Getting the DPI awareness wrong makes it fail and nothing else.
    Set-NativeShellRect -Shell $shell -X 60 -Y 60 -Width 1600 -Height 1000 -Topmost
    Check -Ok (Set-NativeShellForeground -Shell $shell) -Message 'the shell can be brought to the foreground'
    $scale = (Get-NativeShellGeometry -Shell $shell).Scale

    $before = Get-NativeShellCapture -Shell $shell -Path (Join-Path $captureDirectory 'before-click.png')
    $beforeTracks = Get-NativeShellRegionSignature -Bitmap $before `
        -X ([int]($Layout.TrackRows.X * $scale)) -Y ([int]($Layout.TrackRows.Y * $scale)) `
        -Width ([int]($Layout.TrackRows.W * $scale)) -Height ([int]($Layout.TrackRows.H * $scale))
    $before.Dispose()

    Invoke-NativeShellClick -Shell $shell `
        -X ([int]($Layout.FileRow1.X * $scale)) -Y ([int]($Layout.FileRow1.Y * $scale))

    $after = Get-NativeShellCapture -Shell $shell -Path (Join-Path $captureDirectory 'after-click.png')
    $afterTracks = Get-NativeShellRegionSignature -Bitmap $after `
        -X ([int]($Layout.TrackRows.X * $scale)) -Y ([int]($Layout.TrackRows.Y * $scale)) `
        -Width ([int]($Layout.TrackRows.W * $scale)) -Height ([int]($Layout.TrackRows.H * $scale))
    $after.Dispose()

    Check -Ok ($beforeTracks -ne $afterTracks) `
          -Message 'clicking a MIDI file row loads it and fills the Tracks panel' `
          -Detail ("The Tracks panel did not change. Either the click missed, or the file row is no longer at " +
                   "client-logical ($($Layout.FileRow1.X), $($Layout.FileRow1.Y)) -- see the `$Layout block in this file. " +
                   "Compare $captureDirectory\before-click.png against after-click.png.")

    # WM_DPICHANGED. Only real with two monitors at different scales; there is
    # no honest way to fake it, so it is skipped rather than pretended.
    $monitors = @(Get-NativeShellMonitors)
    if ($monitors.Count -lt 2) {
        Write-Host "SKIP a move between monitors at different scales (one monitor attached)"
    } else {
        $other = $monitors | Where-Object { -not $_.Primary } | Select-Object -First 1
        $home = Get-NativeShellGeometry -Shell $shell
        Set-NativeShellRect -Shell $shell -X ($other.Bounds.X + 40) -Y ($other.Bounds.Y + 40) `
                            -Width 1400 -Height 900 -Topmost -SettleMilliseconds 900
        $moved = Get-NativeShellGeometry -Shell $shell
        Check -Ok (-not $shell.Process.HasExited) -Message 'the shell survives a move to the second monitor'
        Check -Ok ($moved.Monitor -ne $home.Monitor) -Message 'the window really landed on the other monitor'
        if ($moved.Dpi -ne $home.Dpi) {
            Check -Ok ($moved.Scale -gt 0) -Message "WM_DPICHANGED was handled ($($home.Dpi) to $($moved.Dpi))"
            $capture = Get-NativeShellCapture -Shell $shell -Path (Join-Path $captureDirectory 'other-monitor.png')
            $capture.Dispose()
        } else {
            Write-Host "SKIP the DPI change itself (both monitors run at $($home.Dpi))"
        }
        Set-NativeShellRect -Shell $shell -X 60 -Y 60 -Width 1400 -Height 900 -Topmost -SettleMilliseconds 900
    }

    # WM_CLOSE, not a kill: the shell saves its settings and joins the engine
    # worker on the way out, and a kill proves neither.
    $exitCode = Stop-NativeShell -Shell $shell
    Check -Ok ($exitCode -eq 0) -Message 'WM_CLOSE exits cleanly' -Detail "exit code $exitCode"
    Check -Ok (Test-Path -LiteralPath (Join-Path $shell.WorkingDirectory 'shell-settings.json')) `
          -Message 'settings are written on the way out'
}
finally {
    if (-not $shell.Process.HasExited) { $shell.Process | Stop-Process -Force }
    if (-not $KeepCaptures -and (Test-Path -LiteralPath $captureDirectory)) {
        Remove-Item -LiteralPath $captureDirectory -Recurse -Force
    }
}

if ($script:failures) { throw "$($script:failures) native check(s) failed." }
Write-Host 'PASS all native window checks'
