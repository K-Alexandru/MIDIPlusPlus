# Native window automation for the ImGui shell.
#
# Dot-source this; `run-native-tests.ps1` is the suite that uses it.
#
# The render harness in `RenderTests.cpp` drives ImGui IO and DX11 directly. That
# reaches everything ImGui owns and nothing Windows owns, which leaves a real
# gap: WM_GETMINMAXINFO, WM_SIZE and ResizeBuffers, WM_DPICHANGED, the message
# pump, and whether a physical click at a physical pixel lands on the control
# drawn there. This drives the built executable through user32 instead.
#
# THE TRAP, and the reason this file exists rather than being rewritten each
# time: this machine's display runs at 125%. MIDI++ is per-monitor DPI aware and
# PowerShell is not, so a script that calls SetCursorPos or CopyFromScreen
# without opting in gets virtualised coordinates. The screen reports 2048x1152
# instead of 2560x1440, every click lands 25% away from where the screenshot
# showed it, and the app looks like it is ignoring input. It is not.
# GetDeviceCaps(LOGPIXELSX) reads 96 from a virtualised process, so nothing
# warns you. Enter-NativeShellDpiAwareness is called by Start-NativeShell and
# must run before the first cursor or capture call.

Set-StrictMode -Version Latest

$script:NativeShellTitle = 'MIDI++ shell (ImGui)'

if (-not ('NativeShellApi' -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public static class NativeShellApi {
    public delegate bool EnumProc(IntPtr window, IntPtr param);

    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr param);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr window, ref Point point);
    [DllImport("user32.dll")] public static extern IntPtr PostMessageW(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, IntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);

    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }

    // Every top-level window the process owns, so a caller can tell the main
    // window from an ImGui platform viewport such as Key Mapping. Titles come
    // back through the Unicode entry point with CharSet.Unicode set: the
    // default Ansi marshalling reads UTF-16 one byte at a time and every title
    // comes back as its own first letter.
    public static string[] Windows(uint pid) {
        var found = new System.Collections.Generic.List<string>();
        EnumWindows((window, param) => {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == pid) {
                var title = new StringBuilder(512);
                GetWindowTextW(window, title, 512);
                found.Add(window.ToInt64() + "|" + IsWindowVisible(window) + "|" + title);
            }
            return true;
        }, IntPtr.Zero);
        return found.ToArray();
    }
}
"@
}

# DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2. Idempotent: the second call in a
# session fails and is meant to.
function Enter-NativeShellDpiAwareness {
    [void][NativeShellApi]::SetProcessDpiAwarenessContext([IntPtr](-4))
    Add-Type -AssemblyName System.Drawing
    Add-Type -AssemblyName System.Windows.Forms
}

function Get-NativeShellWindows {
    param([Parameter(Mandatory)][int] $ProcessId)
    foreach ($row in [NativeShellApi]::Windows([uint32]$ProcessId)) {
        $parts = $row -split '\|', 3
        [pscustomobject]@{
            Handle  = [IntPtr][int64]$parts[0]
            Visible = [bool]::Parse($parts[1])
            Title   = $parts[2]
        }
    }
}

# The main window, never a viewport. Get-Process.MainWindowHandle is not good
# enough here: with viewports enabled it hands back whichever top-level window
# Windows happens to prefer, which during one run was the Key Mapping window,
# and every coordinate after that was measured against the wrong rectangle.
function Wait-NativeShellWindow {
    param(
        [Parameter(Mandatory)][int] $ProcessId,
        [string] $Title = $script:NativeShellTitle,
        [int] $TimeoutSeconds = 20
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $match = Get-NativeShellWindows -ProcessId $ProcessId |
                 Where-Object { $_.Visible -and $_.Title -eq $Title }
        if ($match) { return @($match)[0].Handle }
        Start-Sleep -Milliseconds 200
    }
    throw "No visible window titled '$Title' in process $ProcessId after $TimeoutSeconds s."
}

# Runs the shell out of its own directory so the suite never reads or writes the
# settings, config or MIDI folder the owner is actually using.
function Start-NativeShell {
    param(
        [Parameter(Mandatory)][string] $RepoPath,
        [string] $WorkingDirectory,
        [hashtable] $Preferences,
        [string[]] $MidiFiles = @()
    )
    Enter-NativeShellDpiAwareness

    $source = Join-Path $RepoPath 'build\shell\MIDIShell.exe'
    if (-not (Test-Path -LiteralPath $source)) { throw "Build the shell first: $source is missing." }
    if (-not $WorkingDirectory) { $WorkingDirectory = Join-Path $RepoPath 'build\native-tests' }

    Get-Process -Name 'MIDIShell' -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 400
    if (Test-Path -LiteralPath $WorkingDirectory) { Remove-Item -LiteralPath $WorkingDirectory -Recurse -Force }
    New-Item -ItemType Directory -Path $WorkingDirectory -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $WorkingDirectory
    Copy-Item -LiteralPath (Join-Path $RepoPath 'x64\Release\config.json') -Destination $WorkingDirectory

    # miniMode and velocityExpanded are deliberately not persisted by the app, so
    # every run starts in the full window whatever the last one did.
    $settings = @{ skin = 0; autoSoloPiano = $true; keyMappingOpen = $false; alwaysOnTop = $false; midiFolder = '' }
    if ($Preferences) { foreach ($key in $Preferences.Keys) { $settings[$key] = $Preferences[$key] } }
    if ($MidiFiles.Count) {
        $folder = Join-Path $WorkingDirectory 'midi'
        New-Item -ItemType Directory -Path $folder -Force | Out-Null
        foreach ($file in $MidiFiles) { Copy-Item -LiteralPath $file -Destination $folder }
        $settings['midiFolder'] = $folder
    }
    ($settings | ConvertTo-Json) | Out-File -LiteralPath (Join-Path $WorkingDirectory 'shell-settings.json') -Encoding utf8

    $process = Start-Process -FilePath (Join-Path $WorkingDirectory 'MIDIShell.exe') `
                             -WorkingDirectory $WorkingDirectory -PassThru
    $window = Wait-NativeShellWindow -ProcessId $process.Id
    [pscustomobject]@{
        Process          = $process
        Window           = $window
        WorkingDirectory = $WorkingDirectory
    }
}

# WM_CLOSE, not Stop-Process: the shell saves its settings and joins the engine
# worker on the way out, and killing it proves none of that.
function Stop-NativeShell {
    param(
        [Parameter(Mandatory)] $Shell,
        [int] $TimeoutSeconds = 15
    )
    [void][NativeShellApi]::PostMessageW($Shell.Window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $Shell.Process.WaitForExit($TimeoutSeconds * 1000)) {
        $Shell.Process | Stop-Process -Force
        throw "The shell did not exit within $TimeoutSeconds s of WM_CLOSE."
    }
    $Shell.Process.ExitCode
}

function Set-NativeShellRect {
    param(
        [Parameter(Mandatory)] $Shell,
        [Parameter(Mandatory)][int] $X,
        [Parameter(Mandatory)][int] $Y,
        [Parameter(Mandatory)][int] $Width,
        [Parameter(Mandatory)][int] $Height,
        [switch] $Topmost,
        [int] $SettleMilliseconds = 400
    )
    $after = if ($Topmost) { [IntPtr](-1) } else { [IntPtr]::Zero }
    $flags = if ($Topmost) { 0x0040 } else { 0x4000 }   # SHOWWINDOW : NOZORDER
    [void][NativeShellApi]::SetWindowPos($Shell.Window, $after, $X, $Y, $Width, $Height, $flags)
    Start-Sleep -Milliseconds $SettleMilliseconds
}

function Get-NativeShellGeometry {
    param([Parameter(Mandatory)] $Shell)
    $window = New-Object NativeShellApi+Rect
    $client = New-Object NativeShellApi+Rect
    [void][NativeShellApi]::GetWindowRect($Shell.Window, [ref]$window)
    [void][NativeShellApi]::GetClientRect($Shell.Window, [ref]$client)
    $origin = New-Object NativeShellApi+Point
    [void][NativeShellApi]::ClientToScreen($Shell.Window, [ref]$origin)
    $dpi = [NativeShellApi]::GetDpiForWindow($Shell.Window)
    [pscustomobject]@{
        WindowWidth  = $window.Right - $window.Left
        WindowHeight = $window.Bottom - $window.Top
        ClientWidth  = $client.Right - $client.Left
        ClientHeight = $client.Bottom - $client.Top
        ClientX      = $origin.X
        ClientY      = $origin.Y
        Dpi          = $dpi
        Scale        = $dpi / 96.0
        Monitor      = [NativeShellApi]::MonitorFromWindow($Shell.Window, 2)  # NEAREST
    }
}

function Set-NativeShellForeground {
    param([Parameter(Mandatory)] $Shell, [int] $SettleMilliseconds = 600)
    [void][NativeShellApi]::SetForegroundWindow($Shell.Window)
    Start-Sleep -Milliseconds $SettleMilliseconds
    [NativeShellApi]::GetForegroundWindow() -eq $Shell.Window
}

# Client-relative, because that is how a layout is specified. The jiggle is not
# superstition: a click with no preceding movement leaves the backend's cached
# mouse position wherever it was, and ImGui hit-tests against that rather than
# against where the button went down.
function Invoke-NativeShellClick {
    param(
        [Parameter(Mandatory)] $Shell,
        [Parameter(Mandatory)][int] $X,
        [Parameter(Mandatory)][int] $Y,
        [int] $HoldMilliseconds = 8,
        [int] $SettleMilliseconds = 400
    )
    $geometry = Get-NativeShellGeometry -Shell $Shell
    $screenX = $geometry.ClientX + $X
    $screenY = $geometry.ClientY + $Y
    foreach ($step in 6..0) {
        [void][NativeShellApi]::SetCursorPos(($screenX + $step * 3), ($screenY + $step * 2))
        Start-Sleep -Milliseconds 20
    }
    [void][NativeShellApi]::SetCursorPos($screenX, $screenY)
    Start-Sleep -Milliseconds 120
    [NativeShellApi]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)   # LEFTDOWN
    Start-Sleep -Milliseconds $HoldMilliseconds
    [NativeShellApi]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)   # LEFTUP
    Start-Sleep -Milliseconds $SettleMilliseconds
}

# The client area as it is actually on the glass. Save it or hand it to
# Get-NativeShellRegionSignature; the caller disposes it.
function Get-NativeShellCapture {
    param([Parameter(Mandatory)] $Shell, [string] $Path)
    $geometry = Get-NativeShellGeometry -Shell $Shell
    $bitmap = New-Object System.Drawing.Bitmap $geometry.ClientWidth, $geometry.ClientHeight
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($geometry.ClientX, $geometry.ClientY, 0, 0,
                             (New-Object System.Drawing.Size $geometry.ClientWidth, $geometry.ClientHeight))
    $graphics.Dispose()
    if ($Path) { $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png) }
    $bitmap
}

# A cheap fingerprint of one rectangle, for "did this region change" assertions.
# Sampled on a grid rather than per pixel: the point is to notice a panel filling
# in, and a full-resolution hash would also notice the caret blinking.
function Get-NativeShellRegionSignature {
    param(
        [Parameter(Mandatory)][System.Drawing.Bitmap] $Bitmap,
        [Parameter(Mandatory)][int] $X,
        [Parameter(Mandatory)][int] $Y,
        [Parameter(Mandatory)][int] $Width,
        [Parameter(Mandatory)][int] $Height,
        [int] $Step = 7
    )
    $right = [Math]::Min($X + $Width, $Bitmap.Width)
    $bottom = [Math]::Min($Y + $Height, $Bitmap.Height)
    $hash = [long]17
    for ($py = $Y; $py -lt $bottom; $py += $Step) {
        for ($px = $X; $px -lt $right; $px += $Step) {
            $hash = ($hash * 31 + $Bitmap.GetPixel($px, $py).ToArgb()) -band 0x7FFFFFFFFFFF
        }
    }
    $hash
}

function Get-NativeShellMonitors {
    [System.Windows.Forms.Screen]::AllScreens | ForEach-Object {
        [pscustomobject]@{ Bounds = $_.Bounds; Primary = $_.Primary }
    }
}
