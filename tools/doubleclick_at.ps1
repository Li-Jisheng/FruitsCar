param(
    [int]$X,
    [int]$Y,
    [string]$TitleContains = 'Cocos Creator'
)
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win4 {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, IntPtr dwExtraInfo);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    public struct POINT { public int X, Y; }
}
"@
$proc = Get-Process | Where-Object { $_.MainWindowTitle -like "*$TitleContains*" } | Select-Object -First 1
if (-not $proc) { Write-Output 'NO_WINDOW'; exit 1 }
$hwnd = $proc.MainWindowHandle
[Win4]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 350
$origin = New-Object Win4+POINT
[Win4]::ClientToScreen($hwnd, [ref]$origin) | Out-Null
$px = $origin.X + $X
$py = $origin.Y + $Y
[Win4]::SetCursorPos($px, $py) | Out-Null
Start-Sleep -Milliseconds 150
for ($i = 0; $i -lt 2; $i++) {
    [Win4]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 40
    [Win4]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 80
}
Write-Output ("DBLCLICK client=(" + $X + "," + $Y + ") screen=(" + $px + "," + $py + ")")
