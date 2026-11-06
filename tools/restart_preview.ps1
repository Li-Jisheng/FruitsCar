# 重启 Game View 预览：先确保它已停止，再重新启动，
# 以便加载新编译的脚本。
param(
    [int]$PlayX = 852,
    [int]$PlayY = 23,
    [int]$TimeoutSeconds = 90
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Get-Running {
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'mcp_call.ps1') -Tool get_runtime_state 2>$null
    $text = ($out | Out-String)
    if ($text -match '"running":\s*(true|false)') {
        return ($matches[1] -eq 'true')
    }
    return $null
}
$running = Get-Running
Write-Output ("PREVIEW_RUNNING " + $running)
if ($running -ne $false) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'click_at.ps1') -X $PlayX -Y $PlayY | Out-Null
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 500
        if ((Get-Running) -eq $false) { break }
    }
    Write-Output ("STOPPED " + ((Get-Running) -eq $false))
}
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'click_at.ps1') -X $PlayX -Y $PlayY | Out-Null
$started = $false
for ($i = 0; $i -lt ($TimeoutSeconds * 2); $i++) {
    Start-Sleep -Milliseconds 500
    if ((Get-Running) -eq $true) { $started = $true; break }
}
Write-Output ("STARTED " + $started)
Start-Sleep -Seconds 8
exit $(if ($started) { 0 } else { 1 })
