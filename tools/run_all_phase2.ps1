# Phase-2 回归：环境 -> 构建 -> M12..M18 规则用例 -> 两个关卡的
# autoplay 通关/回放 -> 堵车 -> 证据自检 -> phase2_summary.json
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_all.ps1 -Phase 2
param(
    [int]$Seed = 20261105,
    [switch]$SkipBuild
)
. (Join-Path $PSScriptRoot 'common.ps1')


$root = Get-ProjectRoot
$evidence = Get-EvidenceRoot2
if (-not (Test-Path -LiteralPath $evidence)) {
    New-Item -ItemType Directory -Force -Path $evidence | Out-Null
}
$started = Get-Date
$steps = New-Object System.Collections.Generic.List[object]

function Add-Step {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    $script:steps.Add([ordered]@{ name = $Name; ok = $Ok; detail = $Detail }) | Out-Null
    $state = if ($Ok) { 'PASS' } else { 'FAIL' }
    Write-Output ("STEP $state $Name $Detail")
}

# 1) 环境
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'env_check.ps1') *> $null
$environmentOk = ($LASTEXITCODE -eq 0)
Add-Step -Name 'env_check' -Ok $environmentOk -Detail 'environment.json'

# 2) 构建
$buildOk = $false
if (-not $SkipBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1') *> $null
    $buildOk = ($LASTEXITCODE -eq 0)
} else {
    $buildOk = $null -ne (Get-BinaryPath -Name 'fc_tests')
}
Add-Step -Name 'build' -Ok $buildOk -Detail 'fc_core/fc_host/fc_tests'

if (-not $environmentOk -or -not $buildOk) {
    Write-Output 'RUN_ALL_FAILED env/build'
    exit 1
}

# 3) 规则测试：整套用例（phase 1 + phase 2）必须保持全绿。
$testsLog = Join-Path $evidence 'phase2_run_all.log'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run_tests.ps1') -All -Seed $Seed -Log $testsLog *> $null
$testsOk = ($LASTEXITCODE -eq 0)
$summary = $null
if (Test-Path -LiteralPath $testsLog) {
    $line = Get-Content -LiteralPath $testsLog | Where-Object { $_ -match '"kind":"SUMMARY"' } | Select-Object -Last 1
    if ($line) { $summary = $line | ConvertFrom-Json }
}
Add-Step -Name 'rule_tests' -Ok $testsOk -Detail ($(if ($summary) { "cases=$($summary.cases) pass=$($summary.pass) fail=$($summary.fail)" } else { 'no summary' }))

# 4) 各模块证据日志（每个模块运行自己的用例并保留日志）
$moduleRuns = @(
    @{ module = 'M12'; cases = 'M12' },
    @{ module = 'M13'; cases = 'M13' },
    @{ module = 'M14'; cases = 'M14' },
    @{ module = 'M15'; cases = 'M15' },
    @{ module = 'M16'; cases = 'M16' },
    @{ module = 'M17'; cases = 'M17' },
    @{ module = 'M18'; cases = 'M18' }
)
$modulesOk = $true
foreach ($run in $moduleRuns) {
    $directory = Get-ModuleEvidenceDirectory -Module $run.module -Phase 2
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }
    $log = Join-Path $directory 'run_all.log'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run_tests.ps1') `
        -Module $run.cases -Seed $Seed -Log $log -NoRegression *> $null
    $ok = ($LASTEXITCODE -eq 0)
    if (-not $ok) { $modulesOk = $false }
    Write-Output ("MODULE " + $run.module + " ok=" + $ok)
}
Add-Step -Name 'module_evidence' -Ok $modulesOk -Detail ('modules=' + $moduleRuns.Count)

# 5) 两个关卡的 autoplay 通关 + 确定性回放（E2）
$hostExe = Get-BinaryPath -Name 'fc_host'
$levelRuns = @(
    @{ module = 'M16_level1'; level = 'levels/L_RING_OMEGA.json'; levelId = 'L_RING_OMEGA'; name = 'L1' },
    @{ module = 'M17_level2'; level = 'levels/L_ZIGZAG_WIDE.json'; levelId = 'L_ZIGZAG_WIDE'; name = 'L2' }
)
$autoplayOk = $true
$autoplayResults = @()
foreach ($run in $levelRuns) {
    $directory = Join-Path $evidence $run.module
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }
    $logPath = Join-Path $directory 'host_win.ndjson'
    $replayPath = Join-Path $directory 'replay_win.jsonl'
    $hostOutput = & $hostExe --level $run.level --seed $Seed --autoplay greedy --no-server `
        --exit-on-result --max-ticks 20000 --log $logPath --save-replay $replayPath 2>&1
    $status = 'UNKNOWN'
    $tick = -1
    $hash = ''
    foreach ($line in $hostOutput) {
        $text = [string]$line
        if ($text -match 'status=([A-Z_]+)') { $status = $matches[1] }
        if ($text -match 'tick=(\d+)') { $tick = [int]$matches[1] }
        if ($text -match 'hash=(\d+)') { $hash = $matches[1] }
    }
    $replayOutput = & $hostExe --level $run.level --seed $Seed --replay $replayPath `
        --max-ticks 20000 --out (Join-Path $directory 'host_replay.ndjson') 2>&1
    $replayHash = ''
    $replayTick = -1
    $replayStatus = 'UNKNOWN'
    foreach ($line in $replayOutput) {
        $text = [string]$line
        if ($text -match 'status=([A-Z_]+)') { $replayStatus = $matches[1] }
        if ($text -match 'tick=(-?\d+)') { $replayTick = [int]$matches[1] }
        if ($text -match 'hash=(\d+)') { $replayHash = $matches[1] }
    }
    $ok = ($status -eq 'WIN') -and ($hash -eq $replayHash) -and ($tick -eq $replayTick)
    if (-not $ok) { $autoplayOk = $false }
    $autoplayResults += [ordered]@{
        level = $run.levelId; status = $status; tick = $tick; hash = $hash
        replayStatus = $replayStatus; replayTick = $replayTick; replayHash = $replayHash; ok = $ok
    }
    Write-Output ("AUTOPLAY " + $run.levelId + " status=" + $status + " tick=" + $tick + " hash=" + $hash + " replayHash=" + $replayHash)
}
Add-Step -Name 'autoplay_win' -Ok $autoplayOk -Detail ('levels=' + $levelRuns.Count)

# 6) 新宽关卡上的堵车失败路径（E2）
$jamDir = Join-Path $evidence 'M19_e2e'
if (-not (Test-Path -LiteralPath $jamDir)) {
    New-Item -ItemType Directory -Force -Path $jamDir | Out-Null
}
$jamLog = Join-Path $jamDir 'jam.ndjson'
$jamOutput = & $hostExe --level 'tests/levels/L_p2_jam.json' --seed $Seed --autoplay spam --no-server `
    --exit-on-result --max-ticks 20000 --log $jamLog 2>&1
$jamStatus = 'UNKNOWN'
$jamTick = -1
foreach ($line in $jamOutput) {
    $text = [string]$line
    if ($text -match 'status=([A-Z_]+)') { $jamStatus = $matches[1] }
    if ($text -match 'tick=(\d+)') { $jamTick = [int]$matches[1] }
}
$jamOk = ($jamStatus -eq 'FAIL_TRAFFIC_JAM')
Add-Step -Name 'jam_path' -Ok $jamOk -Detail ('status=' + $jamStatus + ' tick=' + $jamTick)

# 7) 证据自检
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'verify_evidence.ps1') -All -Phase 2 *> $null
$verifyOk = ($LASTEXITCODE -eq 0)
Add-Step -Name 'verify_evidence' -Ok $verifyOk -Detail 'phase 2 modules'

$durationMs = [int]((Get-Date) - $started).TotalMilliseconds
$result = if ($environmentOk -and $buildOk -and $testsOk -and $modulesOk -and $autoplayOk -and $jamOk -and $verifyOk) { 'PASS' } else { 'FAIL' }

$phaseSummary = [ordered]@{
    generatedAt  = (Get-IsoTimestamp)
    phase        = 2
    seed         = $Seed
    result       = $result
    durationMs   = $durationMs
    caseCount    = if ($summary) { [int]$summary.cases } else { 0 }
    passCount    = if ($summary) { [int]$summary.pass } else { 0 }
    failCount    = if ($summary) { [int]$summary.fail } else { 0 }
    skippedCount = if ($summary) { [int]$summary.skipped } else { 0 }
    steps        = $steps
    autoplay     = $autoplayResults
    jam          = [ordered]@{ status = $jamStatus; tick = $jamTick }
    artifacts    = @('phase2_run_all.log', 'phase2_summary.json', 'M16_level1/host_win.ndjson', 'M17_level2/host_win.ndjson', 'M19_e2e/jam.ndjson')
}
$summaryPath = Join-Path $evidence 'phase2_summary.json'
Write-Utf8NoBom -Path $summaryPath -Content ($phaseSummary | ConvertTo-Json -Depth 8)
Write-Output ('PHASE2_SUMMARY ' + $summaryPath)
Write-Output ('PHASE2_RESULT ' + $result)
if ($result -ne 'PASS') { exit 1 }
exit 0
