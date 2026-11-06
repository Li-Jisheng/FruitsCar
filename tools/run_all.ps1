# 一条命令完成回归的总调度脚本。
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_all.ps1            # 阶段 1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_all.ps1 -Phase 2   # 阶段 2
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_all.ps1 -Phase 3   # 框架演进
param(
    [int]$Phase = 1,
    [int]$Seed = 0,
    [switch]$SkipBuild,
    [switch]$CleanBuild
)
. (Join-Path $PSScriptRoot 'common.ps1')

$target = if ($Phase -eq 3) { 'run_all_phase3.ps1' } elseif ($Phase -eq 2) { 'run_all_phase2.ps1' } else { 'run_all_phase1.ps1' }
$arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot $target))
if ($Seed -ne 0) {
    $arguments += @('-Seed', $Seed)
}
if ($SkipBuild) {
    $arguments += '-SkipBuild'
}
if ($CleanBuild) {
    $arguments += '-CleanBuild'
}
& powershell @arguments
exit $LASTEXITCODE
