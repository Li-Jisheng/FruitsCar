param(
    [int]$X,
    [int]$Y,
    [string]$TitleContains = 'Cocos Creator',
    [switch]$NoFocus
)
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win3 {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, IntPtr dwExtraInfo);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    public struct POINT { public int X, Y; }
}
"@
$proc = Get-Process | Where-Object { $_.MainWindowTitle -like "*$TitleContains*" } | Select-Object -First 1
if (-not $proc) { Write-Output 'NO_WINDOW'; exit 1 }
$hwnd = $proc.MainWindowHandle
if (-not $NoFocus) {
    [Win3]::ShowWindow($hwnd, 3) | Out-Null
    Start-Sleep -Milliseconds 350
    [Win3]::SetForegroundWindow($hwnd) | Out-Null
    Start-Sleep -Milliseconds 350
}
$origin = New-Object Win3+POINT
$origin.X = 0
$origin.Y = 0
[Win3]::ClientToScreen($hwnd, [ref]$origin) | Out-Null
$px = $origin.X + $X
$py = $origin.Y + $Y
[Win3]::SetCursorPos($px, $py) | Out-Null
Start-Sleep -Milliseconds 120
[Win3]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 60
[Win3]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
Write-Output ("CLICK client=(" + $X + "," + $Y + ") screen=(" + $px + "," + $py + ")")
