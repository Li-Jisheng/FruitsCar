# 用客户端一致性报告拼出 M10 的 "run_all.log"（NDJSON + SUMMARY），
# 这样 verify_evidence.ps1 就能像检查其他模块日志一样检查它。
param(
    [string]$Directory = 'Docs/evidence/phase1/M10_client_view'
)
. (Join-Path $PSScriptRoot 'common.ps1')

$dir = Resolve-FullPath $Directory
$consistency = Join-Path $dir 'consistency_nodes.log'
if (-not (Test-Path -LiteralPath $consistency)) {
    Write-Output 'M10_LOG_FAILED no consistency_nodes.log'
    exit 1
}

$lines = @(Get-Content -LiteralPath $consistency | Where-Object { $_.Trim().Length -gt 0 })
$seq = 0
$fail = 0
$pass = 0
$out = New-Object System.Collections.Generic.List[string]
$runId = 'M10-' + (Get-Date -Format 'yyyyMMdd-HHmm')
foreach ($line in $lines) {
    $obj = $line | ConvertFrom-Json
    $seq++
    $ok = ([int]$obj.mismatches -eq 0)
    if ($ok) { $pass++ } else { $fail++ }
    $assert = [ordered]@{
        ts    = (Get-IsoTimestamp)
        run   = $runId
        case  = 'TC-M10-07'
        seq   = $seq
        kind  = 'ASSERT'
        tick  = [int]$obj.tick
        ok    = $ok
        check = 'node_counts'
        expected = [ordered]@{ fruit = [int]$obj.fruitCells; road = [int]$obj.roadCars; lane = [int]$obj.laneCars; parked = [int]$obj.occupiedSlots }
        actual = [ordered]@{ fruit = [int]$obj.fruitNodes; road = [int]$obj.roadNodes; lane = [int]$obj.laneNodes; parked = [int]$obj.parkedNodes }
    }
    $out.Add(($assert | ConvertTo-Json -Compress -Depth 6)) | Out-Null
}
$seq++
$summary = [ordered]@{
    ts         = (Get-IsoTimestamp)
    run        = $runId
    case       = ''
    seq        = $seq
    kind       = 'SUMMARY'
    tick       = 0
    cases      = 1
    pass       = $(if ($fail -eq 0) { 1 } else { 0 })
    fail       = $(if ($fail -eq 0) { 0 } else { 1 })
    skipped    = 0
    result     = $(if ($fail -eq 0) { 'PASS' } else { 'FAIL' })
    durationMs = 0
}
$out.Add(($summary | ConvertTo-Json -Compress -Depth 6)) | Out-Null
Write-Utf8NoBom -Path (Join-Path $dir 'run_all.log') -Content (($out -join "`r`n") + "`r`n")
Write-Output ('M10_LOG lines=' + $out.Count + ' assertions=' + ($pass + $fail) + ' mismatched=' + $fail)
exit 0
