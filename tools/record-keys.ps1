<#
.SYNOPSIS
  Log every key event, physical and injected, so a stray global hotkey can be
  identified by what was actually pressed at the moment it fired.

.DESCRIPTION
  Written because "Saving clip" kept appearing while playing in Roblox and
  three plausible culprits were all ruled out by inspection: AMD's hotkeys are
  off, Game Bar capture is disabled in the registry, Overwolf is not running.
  Guessing at the fourth is not worth another wrong answer.

  A WH_KEYBOARD_LL hook sees both what you press and what MIDI++ injects, and
  marks which is which, so the log shows the exact combination the game
  received when the toast appeared.

  This hook runs in front of every keystroke on the machine, so it costs a
  little latency on all of them. Use it to catch the bug, not to play well.

  Nothing is sent anywhere. The log is a local CSV you can read yourself.

.EXAMPLE
  .\tools\record-keys.ps1
  .\tools\record-keys.ps1 -Path C:\Users\Me\keys.csv -Seconds 300
#>
[CmdletBinding()]
param(
    [string]$Path = (Join-Path $env:TEMP ("midi-keys-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + ".csv")),
    [int]$Seconds = 0   # 0 means run until Ctrl+C
)

$ErrorActionPreference = 'Stop'

$source = @'
using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using System.Text;

public static class KeyLog {
    public const int WH_KEYBOARD_LL = 13;
    public const int LLKHF_INJECTED = 0x10;

    [StructLayout(LayoutKind.Sequential)]
    public struct KBDLLHOOKSTRUCT {
        public uint vkCode, scanCode, flags, time;
        public IntPtr extraInfo;
    }

    public delegate IntPtr HookProc(int code, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr SetWindowsHookEx(int idHook, HookProc fn, IntPtr module, uint thread);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool UnhookWindowsHookEx(IntPtr hook);
    [DllImport("user32.dll")]
    public static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr wParam, IntPtr lParam);
    [DllImport("kernel32.dll")]
    public static extern IntPtr GetModuleHandle(string name);
    [DllImport("user32.dll")]
    public static extern short GetAsyncKeyState(int vk);
    [DllImport("user32.dll")]
    public static extern int GetForegroundWindow();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(int hwnd, StringBuilder text, int count);

    public static readonly ConcurrentQueue<string> Lines = new ConcurrentQueue<string>();
    private static IntPtr _hook = IntPtr.Zero;
    private static HookProc _proc;   // kept alive, or the CLR collects it and the hook dies

    private static bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

    private static string Title() {
        var sb = new StringBuilder(160);
        GetWindowText(GetForegroundWindow(), sb, sb.Capacity);
        return sb.ToString().Replace('"', '\'');
    }

    private static IntPtr Callback(int code, IntPtr wParam, IntPtr lParam) {
        if (code >= 0) {
            var info = (KBDLLHOOKSTRUCT)Marshal.PtrToStructure(lParam, typeof(KBDLLHOOKSTRUCT));
            int message = wParam.ToInt32();
            bool down = message == 0x0100 || message == 0x0104;   // WM_KEYDOWN, WM_SYSKEYDOWN
            bool up = message == 0x0101 || message == 0x0105;
            if (down || up) {
                var mods = new StringBuilder();
                if (Down(0x11)) mods.Append("ctrl+");
                if (Down(0x10)) mods.Append("shift+");
                if (Down(0x12)) mods.Append("alt+");
                if (Down(0x5B) || Down(0x5C)) mods.Append("win+");
                Lines.Enqueue(string.Format("{0:HH:mm:ss.fff},{1},{2},0x{3:X2},0x{4:X2},{5},\"{6}{7}\",\"{8}\"",
                    DateTime.Now,
                    down ? "down" : "up",
                    (info.flags & LLKHF_INJECTED) != 0 ? "injected" : "physical",
                    info.vkCode, info.scanCode,
                    (char)((info.vkCode >= 32 && info.vkCode < 127) ? info.vkCode : 32),
                    mods.ToString(),
                    KeyName((int)info.vkCode),
                    Title()));
            }
        }
        return CallNextHookEx(_hook, code, wParam, lParam);
    }

    private static string KeyName(int vk) {
        if (vk >= 0x30 && vk <= 0x39) return ((char)vk).ToString();
        if (vk >= 0x41 && vk <= 0x5A) return ((char)vk).ToString().ToLowerInvariant();
        switch (vk) {
            case 0x10: return "shift"; case 0xA0: return "lshift"; case 0xA1: return "rshift";
            case 0x11: return "ctrl";  case 0xA2: return "lctrl";  case 0xA3: return "rctrl";
            case 0x12: return "alt";   case 0xA4: return "lalt";   case 0xA5: return "ralt";
            case 0x5B: return "lwin";  case 0x5C: return "rwin";
            case 0x20: return "space"; case 0x0D: return "enter";  case 0x09: return "tab";
            case 0x1B: return "esc";   case 0x2C: return "printscreen";
            default:
                if (vk >= 0x70 && vk <= 0x87) return "f" + (vk - 0x6F);
                return "vk" + vk.ToString("X2");
        }
    }

    public static bool Start() {
        _proc = Callback;
        _hook = SetWindowsHookEx(WH_KEYBOARD_LL, _proc, GetModuleHandle(null), 0);
        return _hook != IntPtr.Zero;
    }

    public static void Stop() {
        if (_hook != IntPtr.Zero) { UnhookWindowsHookEx(_hook); _hook = IntPtr.Zero; }
    }
}
'@

Add-Type -AssemblyName System.Windows.Forms | Out-Null
Add-Type -TypeDefinition $source -Language CSharp | Out-Null

if (-not [KeyLog]::Start()) { throw "SetWindowsHookEx failed: $([ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error()).Message)" }

'time,direction,source,vk,scan,char,combo,window' | Out-File -FilePath $Path -Encoding utf8
Write-Host "Recording every key event to $Path"
Write-Host "Physical presses and MIDI++'s injected ones are both logged and labelled."
Write-Host "Play until the clip toast appears, then stop with Ctrl+C and read the last lines."
Write-Host ""

$deadline = if ($Seconds -gt 0) { (Get-Date).AddSeconds($Seconds) } else { [DateTime]::MaxValue }
try {
    while ((Get-Date) -lt $deadline) {
        # The hook only fires while this thread pumps messages.
        [System.Windows.Forms.Application]::DoEvents()
        $line = $null
        $batch = New-Object System.Collections.Generic.List[string]
        while ([KeyLog]::Lines.TryDequeue([ref]$line)) { $batch.Add($line) }
        if ($batch.Count) { $batch | Out-File -FilePath $Path -Encoding utf8 -Append }
        Start-Sleep -Milliseconds 15
    }
}
finally {
    [KeyLog]::Stop()
    $line = $null
    while ([KeyLog]::Lines.TryDequeue([ref]$line)) { $line | Out-File -FilePath $Path -Encoding utf8 -Append }
    Write-Host ""
    Write-Host "Stopped. Log written to $Path"
}
