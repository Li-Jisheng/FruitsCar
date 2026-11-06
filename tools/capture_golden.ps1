# A-08（任务书 9 T-40）的黄金基线：用确定性的贪心自动播放跑六个 v1 关卡 600 tick，
# 每 10 tick 记录一条 stateHash。
# 请在改动 core 之前先运行本脚本；M20 的回归测试会重放同一循环，
# 并对每一行做比对。
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/capture_golden.ps1
param(
    [int]$Seed = 20261104,
    [int]$Ticks = 600,
    [int]$Every = 10,
    [string]$OutDir = 'Docs/evidence/framework/M_golden'
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$exe = Get-BinaryPath -Name 'fc_host'
if (-not $exe) { Write-Output 'GOLDEN_FAILED fc_host.exe not found'; exit 1 }

$outRoot = Resolve-FullPath $OutDir
if (-not (Test-Path -LiteralPath $outRoot)) {
    New-Item -ItemType Directory -Force -Path $outRoot | Out-Null
}

$levels = @('L01_default', 'L02_8x4', 'L03_vertical', 'L04_wide0', 'L05_ring', 'L06_bent')
$target = Join-Path $outRoot 'hash_baseline.log'
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# FruitsCar v1 golden stateHash baseline (task book 9 T-40 / A-08)') | Out-Null
$lines.Add('# seed=' + $Seed + ' ticks=' + $Ticks + ' every=' + $Every + ' autoplay=greedy') | Out-Null
$lines.Add('# format: level|tick|hash  (final line: level|FINAL|status|resultTick|hash)') | Out-Null

foreach ($level in $levels) {
    $levelPath = 'tests/levels/' + $level + '.json'
    $trace = Join-Path $outRoot ('trace_' + $level + '.log')
    $output = & $exe --level $levelPath --seed $Seed --autoplay greedy --no-server `
        --max-ticks $Ticks --hash-trace $trace --hash-every $Every 2>&1
    $exit = $LASTEXITCODE
    $status = 'UNKNOWN'
    foreach ($line in $output) {
        $text = [string]$line
        if ($text -match 'status=([A-Z_]+)') { $status = $matches[1] }
    }
    if (-not (Test-Path -LiteralPath $trace)) {
        Write-Output ('GOLDEN_FAILED no trace for ' + $level + ' exit=' + $exit)
        exit 1
    }
    $count = 0
    foreach ($line in (Get-Content -LiteralPath $trace)) {
        $text = [string]$line
        if ($text.StartsWith('#')) { continue }
        if ($text -match '^tick=(\d+) hash=(\d+)$') {
            $lines.Add($level + '|' + $matches[1] + '|' + $matches[2]) | Out-Null
            $count++
        } elseif ($text -match '^final tick=(\d+) status=([A-Z_]+) resultTick=(-?\d+) hash=(\d+)$') {
            $lines.Add($level + '|FINAL|' + $matches[2] + '|' + $matches[3] + '|' + $matches[4]) | Out-Null
        }
    }
    Write-Output ('GOLDEN ' + $level + ' samples=' + $count + ' hostStatus=' + $status)
    Remove-Item -LiteralPath $trace -Force
}

Write-Utf8NoBom -Path $target -Content (($lines -join "`n") + "`n")
Write-Output ('GOLDEN_BASELINE ' + $target + ' lines=' + $lines.Count)
exit 0
