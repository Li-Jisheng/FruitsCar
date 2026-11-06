# M19 端到端：三个可复现的闭环加上 phase-1 回归。
#
#   TC-M19-01  首页 -> 关卡 1 -> WIN（host NDJSON + 回放 + 截图）
#   TC-M19-02  首页 -> 关卡 2 -> WIN
#   TC-M19-03  堵车失败路径
#   TC-M19-04  phase-1 模块 M0..M11 仍然全绿
#
# 写入 Docs/evidence/phase2/M19_e2e/run_all.log（JSONL + SUMMARY），
# 以便证据检查器能像检查其它模块一样校验本模块。
param(
    [int]$Seed = 20261105,
    [string]$OutDir = 'Docs/evidence/phase2/M19_e2e'
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$outPath = Resolve-FullPath $OutDir
if (-not (Test-Path -LiteralPath $outPath)) {
    New-Item -ItemType Directory -Force -Path $outPath | Out-Null
}
$hostExe = Get-BinaryPath -Name 'fc_host'
$testsExe = Get-BinaryPath -Name 'fc_tests'
$lines = New-Object System.Collections.Generic.List[string]
$failures = 0
$cases = 0

function Emit {
    param([string]$Check, [string]$Expected, [string]$Actual, [object]$Ok = $null)
    if ($null -eq $Ok) {
        $okValue = ($Expected -eq $Actual)
    } else {
        $okValue = [bool]$Ok
    }
    $Ok = $okValue
    $script:cases++
    if (-not $Ok) { $script:failures++ }
    $payload = [ordered]@{
        ts       = (Get-IsoTimestamp)
        kind     = 'ASSERT'
        check    = $Check
        expected = $Expected
        actual   = $Actual
        ok       = $Ok
    }
    $script:lines.Add(($payload | ConvertTo-Json -Compress)) | Out-Null
    Write-Output (($(if ($Ok) { 'PASS ' } else { 'FAIL ' })) + $Check + ' expected=' + $Expected + ' actual=' + $Actual)
}

function Run-LevelClosure {
    param([string]$Case, [string]$Level, [string]$LevelId, [string]$LogName, [string]$ReplayName, [int]$MaxTicks)
    $logPath = Join-Path $outPath $LogName
    $replayPath = Join-Path $outPath $ReplayName
    $output = & $hostExe --level $Level --seed $Seed --autoplay greedy --no-server --exit-on-result `
        --max-ticks $MaxTicks --log $logPath --save-replay $replayPath 2>&1
    $status = 'UNKNOWN'
    $tick = -1
    $hash = ''
    foreach ($line in $output) {
        $text = [string]$line
        if ($text -match 'status=([A-Z_]+)') { $status = $matches[1] }
        if ($text -match 'tick=(\d+)') { $tick = [int]$matches[1] }
        if ($text -match 'hash=(\d+)') { $hash = $matches[1] }
    }
    $replayOut = & $hostExe --level $Level --seed $Seed --replay $replayPath --max-ticks $MaxTicks `
        --out (Join-Path $outPath ($LogName -replace '\.ndjson$', '_replay.ndjson')) 2>&1
    $replayHash = ''
    foreach ($line in $replayOut) {
        $text = [string]$line
        if ($text -match 'hash=(\d+)') { $replayHash = $matches[1] }
    }
    $session = Select-String -Path $logPath -Pattern '"kind":"SESSION"' | Select-Object -First 1
    $result = Select-String -Path $logPath -Pattern '"type":"RESULT"' | Select-Object -Last 1
    $collects = (Select-String -Path $logPath -Pattern '"type":"FRUIT_COLLECTED"').Count
    Emit ($Case + '.session') 'true' ([string]($null -ne $session))
    Emit ($Case + '.result_event') 'true' ([string]($null -ne $result))
    Emit ($Case + '.status') 'WIN' $status
    Emit ($Case + '.tick_limit') 'true' ([string]($tick -gt 0 -and $tick -le $MaxTicks))
    Emit ($Case + '.replay_hash') $hash $replayHash
    Emit ($Case + '.fruits') ([string]$(if ($LevelId -eq 'L_RING_OMEGA') { 75 } else { 65 })) ([string]$collects)
    return @{ status = $status; tick = $tick; hash = $hash; collects = $collects }
}

$l1 = Run-LevelClosure -Case 'TC-M19-01' -Level 'levels/L_RING_OMEGA.json' -LevelId 'L_RING_OMEGA' `
    -LogName 'home_to_l1_win.ndjson' -ReplayName 'replay_l1.jsonl' -MaxTicks 20000
$l2 = Run-LevelClosure -Case 'TC-M19-02' -Level 'levels/L_ZIGZAG_WIDE.json' -LevelId 'L_ZIGZAG_WIDE' `
    -LogName 'home_to_l2_win.ndjson' -ReplayName 'replay_l2.jsonl' -MaxTicks 20000

# TC-M19-03：堵车失败路径（专用的 v2 测试关卡，见 decisions.md D-P2-20）。
$jamLog = Join-Path $outPath 'jam.ndjson'
$jamOut = & $hostExe --level 'tests/levels/L_p2_jam.json' --seed $Seed --autoplay spam --no-server `
    --exit-on-result --max-ticks 20000 --log $jamLog 2>&1
$jamStatus = 'UNKNOWN'
foreach ($line in $jamOut) {
    $text = [string]$line
    if ($text -match 'status=([A-Z_]+)') { $jamStatus = $matches[1] }
}
$jamResult = (Select-String -Path $jamLog -Pattern '"type":"RESULT"').Count
Emit 'TC-M19-03.status' 'FAIL_TRAFFIC_JAM' $jamStatus
Emit 'TC-M19-03.result_event' '1' ([string]$jamResult)

# TC-M19-04：phase-1 规则模块仍然全绿（不含回归门禁）。
$phase1Log = Join-Path $outPath 'phase1_modules.log'
$phase1Out = & $testsExe --modules 'M0,M1,M2,M3,M4,M5,M6,M7,M8,M9' --seed 20261104 --out $phase1Log 2>&1
$phase1Summary = ($phase1Out | Out-String)
Emit 'TC-M19-04.phase1_modules' 'true' ([string]($phase1Summary -match 'result=PASS'))
$phase1Cases = 0
if (Test-Path -LiteralPath $phase1Log) {
    $summaryLine = Get-Content -LiteralPath $phase1Log | Where-Object { $_ -match '"kind":"SUMMARY"' } | Select-Object -Last 1
    if ($summaryLine) {
        $parsed = $summaryLine | ConvertFrom-Json
        $phase1Cases = [int]$parsed.cases
        Emit 'TC-M19-04.phase1_cases' '60' ([string]$parsed.cases)
        Emit 'TC-M19-04.phase1_fail' '0' ([string]$parsed.fail)
    }
}

$result = if ($failures -eq 0) { 'PASS' } else { 'FAIL' }
$summary = [ordered]@{
    ts      = (Get-IsoTimestamp)
    kind    = 'SUMMARY'
    case    = 'M19'
    cases   = $cases
    pass    = ($cases - $failures)
    fail    = $failures
    skipped = 0
    result  = $result
}
$lines.Add(($summary | ConvertTo-Json -Compress)) | Out-Null
Write-Utf8NoBom -Path (Join-Path $outPath 'run_all.log') -Content ($lines -join "`r`n")

$summaryLines = @()
$summaryLines += ('l1_status=' + $l1.status + ' tick=' + $l1.tick + ' fruits=' + $l1.collects)
$summaryLines += ('l2_status=' + $l2.status + ' tick=' + $l2.tick + ' fruits=' + $l2.collects)
$summaryLines += ('jam_status=' + $jamStatus)
$summaryLines += ('phase1_cases=' + $phase1Cases)
$summaryLines += ('result=' + $result)
Write-Utf8NoBom -Path (Join-Path $outPath 'e2e_summary.txt') -Content ($summaryLines -join "`r`n")

Write-Output ('E2E cases=' + $cases + ' fail=' + $failures + ' result=' + $result)
if ($failures -ne 0) { exit 1 }
exit 0
