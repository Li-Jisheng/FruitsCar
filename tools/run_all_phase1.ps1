# 一键回归：环境 -> 构建 -> 全部规则用例 -> 端到端
# host 运行（通关/堵车）-> 证据自检 -> phase1_summary.json。
param(
    [int]$Seed = 20261104,
    [switch]$SkipBuild
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$evidence = Get-EvidenceRoot
$started = Get-Date
$steps = New-Object System.Collections.Generic.List[object]

function Add-Step {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    $script:steps.Add([ordered]@{ name = $Name; ok = $Ok; detail = $Detail }) | Out-Null
    $state = if ($Ok) { 'PASS' } else { 'FAIL' }
    Write-Output ("STEP $state $Name $Detail")
}

# 1) 环境
$environmentOk = $false
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

# 3) 全部规则用例
$testsLog = Join-Path $evidence 'phase1_run_all.log'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run_tests.ps1') -All -Seed $Seed -Log $testsLog *> $null
$testsOk = ($LASTEXITCODE -eq 0)
$summary = $null
if (Test-Path -LiteralPath $testsLog) {
    $line = Get-Content -LiteralPath $testsLog | Where-Object { $_ -match '"kind":"SUMMARY"' } | Select-Object -Last 1
    if ($line) { $summary = $line | ConvertFrom-Json }
}
Add-Step -Name 'rule_tests' -Ok $testsOk -Detail ($(if ($summary) { "cases=$($summary.cases) pass=$($summary.pass) fail=$($summary.fail)" } else { 'no summary' }))

# 4) 端到端 host 运行（E2 证据）
$hostExe = Get-BinaryPath -Name 'fc_host'
$e2e = Join-Path $evidence 'M11_e2e'
if (-not (Test-Path -LiteralPath $e2e)) { New-Item -ItemType Directory -Force -Path $e2e | Out-Null }

$e2eRuns = @(
    @{ name = 'win_20261104'; level = 'tests/levels/L01_default.json'; seed = 20261104; mode = 'greedy'; expect = 'WIN' },
    @{ name = 'win_7';        level = 'tests/levels/L01_default.json'; seed = 7;        mode = 'greedy'; expect = 'WIN' },
    @{ name = 'win_42';       level = 'tests/levels/L01_default.json'; seed = 42;       mode = 'greedy'; expect = 'WIN' },
    @{ name = 'jam_20261104'; level = 'tests/levels/L_jam.json';       seed = 20261104; mode = 'spam';   expect = 'FAIL_TRAFFIC_JAM' }
)

$e2eOk = $true
$e2eResults = @()
foreach ($run in $e2eRuns) {
    $logPath = Join-Path $e2e ('host_' + $run.name + '.ndjson')
    $replayPath = Join-Path $e2e ('replay_' + $run.name + '.jsonl')
    $hostOutput = & $hostExe --level $run.level --seed $run.seed --autoplay $run.mode --no-server `
        --exit-on-result --max-ticks 40000 --log $logPath --save-replay $replayPath 2>&1
    $status = 'UNKNOWN'
    $tick = -1
    $hash = ''
    foreach ($line in $hostOutput) {
        $text = [string]$line
        if ($text -match 'status=([A-Z_]+)') { $status = $matches[1] }
        if ($text -match 'tick=(\d+)') { $tick = [int]$matches[1] }
        if ($text -match 'hash=(\d+)') { $hash = $matches[1] }
    }
    $ok = ($status -eq $run.expect)
    if (-not $ok) { $e2eOk = $false }
    $e2eResults += [ordered]@{ run = $run.name; expected = $run.expect; status = $status; tick = $tick; hash = $hash; ok = $ok }
    Write-Output ("E2E " + $run.name + " status=" + $status + " tick=" + $tick + " hash=" + $hash)
}
Add-Step -Name 'e2e_host_runs' -Ok $e2eOk -Detail ('runs=' + $e2eRuns.Count)

# 5) 证据自检
$verifyOk = $false
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'verify_evidence.ps1') -All *> $null
$verifyOk = ($LASTEXITCODE -eq 0)
Add-Step -Name 'verify_evidence' -Ok $verifyOk -Detail 'all modules'

$durationMs = [int]((Get-Date) - $started).TotalMilliseconds
$result = if ($environmentOk -and $buildOk -and $testsOk -and $e2eOk -and $verifyOk) { 'PASS' } else { 'FAIL' }

$phaseSummary = [ordered]@{
    generatedAt   = (Get-IsoTimestamp)
    seed          = $Seed
    result        = $result
    durationMs    = $durationMs
    caseCount     = if ($summary) { [int]$summary.cases } else { 0 }
    passCount     = if ($summary) { [int]$summary.pass } else { 0 }
    failCount     = if ($summary) { [int]$summary.fail } else { 0 }
    skippedCount  = if ($summary) { [int]$summary.skipped } else { 0 }
    steps         = $steps
    e2e           = $e2eResults
    artifacts     = @('phase1_run_all.log', 'phase1_summary.json', 'M11_e2e/host_*.ndjson')
}
$summaryPath = Join-Path $evidence 'phase1_summary.json'
Write-Utf8NoBom -Path $summaryPath -Content ($phaseSummary | ConvertTo-Json -Depth 8)
Write-Output ('PHASE1_SUMMARY ' + $summaryPath)
Write-Output ('PHASE1_RESULT ' + $result)
if ($result -ne 'PASS') { exit 1 }
exit 0
