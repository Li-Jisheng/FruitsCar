# 在两点之间拖动鼠标（用于滚动主界面的关卡卡片列表）。
param(
    [Parameter(Mandatory = $true)][int]$X1,
    [Parameter(Mandatory = $true)][int]$Y1,
    [Parameter(Mandatory = $true)][int]$X2,
    [Parameter(Mandatory = $true)][int]$Y2,
    [int]$Steps = 12,
    [int]$HoldMs = 40
)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Threading;

public class FcMouseDrag
{
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, int dx, int dy, uint dwData, int dwExtraInfo);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    public const uint LEFTDOWN = 0x0002;
    public const uint LEFTUP = 0x0004;

    public static void Drag(int x1, int y1, int x2, int y2, int steps, int holdMs)
    {
        SetCursorPos(x1, y1);
        Thread.Sleep(120);
        mouse_event(LEFTDOWN, 0, 0, 0, 0);
        Thread.Sleep(80);
        for (int i = 1; i <= steps; i++)
        {
            int x = x1 + (x2 - x1) * i / steps;
            int y = y1 + (y2 - y1) * i / steps;
            SetCursorPos(x, y);
            Thread.Sleep(holdMs);
        }
        Thread.Sleep(120);
        mouse_event(LEFTUP, 0, 0, 0, 0);
    }
}
'@
[FcMouseDrag]::Drag($X1, $Y1, $X2, $Y2, $Steps, $HoldMs)
Write-Output ("DRAGGED " + $X1 + "," + $Y1 + " -> " + $X2 + "," + $Y2)
