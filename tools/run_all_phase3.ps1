# Phase-3 回归与证据：环境 -> 干净构建 -> 全部规则用例 ->
# 各模块证据 -> 弯道关卡 autoplay + 回放 -> 视觉证据检查 ->
# 动画开/关一致性（A-09）-> 证据自检 -> phase3_summary。
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_all.ps1 -Phase 3
param(
    [int]$Seed = 20261107,
    [switch]$SkipBuild,
    [switch]$CleanBuild
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$evidence = Get-EvidenceRoot3
if (-not (Test-Path -LiteralPath $evidence)) {
    New-Item -ItemType Directory -Force -Path $evidence | Out-Null
}
$started = Get-Date
$steps = New-Object System.Collections.Generic.List[object]
$python = $env:FC_PYTHON
if (-not $python) {
    $python = 'D:\ProgrammingSoftware\DeepseekHarness\dsh-home\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'
}

function Add-Step {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    $script:steps.Add([ordered]@{ name = $Name; ok = $Ok; detail = $Detail }) | Out-Null
    $state = if ($Ok) { 'PASS' } else { 'FAIL' }
    Write-Host ("STEP $state $Name $Detail")
}

# 1) 环境 -------------------------------------------------------------------
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'env_check.ps1') *> $null
$environmentOk = ($LASTEXITCODE -eq 0)
Add-Step -Name 'env_check' -Ok $environmentOk -Detail 'environment.json'

# 2) 构建（G1：告警即错误，验收运行使用 -Clean） ------------------------------
$buildOk = $false
if (-not $SkipBuild) {
    $buildArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'build.ps1'))
    if ($CleanBuild) { $buildArgs += '-Clean' }
    & powershell @buildArgs *> $null
    $buildOk = ($LASTEXITCODE -eq 0)
} else {
    $buildOk = $null -ne (Get-BinaryPath -Name 'fc_tests')
}
$buildLog = Join-Path (Get-EvidenceRoot -Phase 1) 'M00_harness\build.log'
$warnings = 0
$errors = 0
if (Test-Path -LiteralPath $buildLog) {
    $lines = Get-Content -LiteralPath $buildLog
    $warnings = ($lines | Where-Object { $_ -match 'warning C|warning:' }).Count
    $errors = ($lines | Where-Object { $_ -match 'error C|error LNK|error MSB' }).Count
}
Add-Step -Name 'build' -Ok ($buildOk -and $warnings -eq 0 -and $errors -eq 0) `
    -Detail ("targets=3 warnings=$warnings errors=$errors clean=$([bool]$CleanBuild)")
if (-not $environmentOk -or -not $buildOk) {
    Write-Host 'RUN_ALL_FAILED env/build'
    exit 1
}

# 3) 完整规则用例集（phase 1 + 2 + 3）--------------------------------------
$testsLog = Join-Path $evidence 'phase3_run_all.log'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run_tests.ps1') `
    -All -Seed $Seed -Log $testsLog *> $null
$testsOk = ($LASTEXITCODE -eq 0)
$summary = $null
if (Test-Path -LiteralPath $testsLog) {
    $line = Get-Content -LiteralPath $testsLog | Where-Object { $_ -match '"kind":"SUMMARY"' } | Select-Object -Last 1
    if ($line) { $summary = $line | ConvertFrom-Json }
}
Add-Step -Name 'rule_tests' -Ok $testsOk `
    -Detail ($(if ($summary) { "cases=$($summary.cases) pass=$($summary.pass) fail=$($summary.fail)" } else { 'no summary' }))

# 4) 各模块证据日志（M20..M26）--------------------------------------------
$moduleRuns = @('M20', 'M21', 'M22', 'M23', 'M24', 'M25', 'M26')
$modulesOk = $true
foreach ($module in $moduleRuns) {
    $directory = Get-ModuleEvidenceDirectory -Module $module -Phase 3
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }
    $log = Join-Path $directory 'run_all.log'
    if ($module -in @('M24', 'M25')) {
        # 视觉模块没有自己的单元用例；它们的证据是
        # 截图包加上客户端一致性报告。这里的日志是
        # 回归运行（M20..M23 + M26 使用相同的渲染数据）。
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run_tests.ps1') `
            -Module 'M22' -Seed $Seed -Log $log -NoRegression *> $null
    } else {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run_tests.ps1') `
            -Module $module -Seed $Seed -Log $log -NoRegression *> $null
    }
    $ok = ($LASTEXITCODE -eq 0)
    if (-not $ok) { $modulesOk = $false }
    Write-Host ("MODULE " + $module + " ok=" + $ok)
}
Add-Step -Name 'module_evidence' -Ok $modulesOk -Detail ('modules=' + $moduleRuns.Count)

# 5) 弯道关卡：autoplay 通关、回放确定性、hash 轨迹、终态 -------------------
$hostExe = Get-BinaryPath -Name 'fc_host'
$levelRuns = @(
    @{ id = 'L_CURVE_RING'; file = 'levels/L_CURVE_RING.json' },
    @{ id = 'L_CURVE_S'; file = 'levels/L_CURVE_S.json' },
    @{ id = 'L_CURVE_POLY'; file = 'levels/L_CURVE_POLY.json' }
)
$e2Dir = Join-Path $evidence 'M27_e2e'
if (-not (Test-Path -LiteralPath $e2Dir)) { New-Item -ItemType Directory -Force -Path $e2Dir | Out-Null }
$autoplayOk = $true
$autoplayResults = @()
foreach ($run in $levelRuns) {
    $logPath = Join-Path $e2Dir ("host_win_" + $run.id + ".ndjson")
    $replayPath = Join-Path $e2Dir ("replay_" + $run.id + ".jsonl")
    $tracePath = Join-Path $e2Dir ("trace_" + $run.id + ".log")
    $dumpPath = Join-Path $e2Dir ("final_" + $run.id + ".json")
    $output = & $hostExe --level $run.file --seed $Seed --autoplay greedy --no-server `
        --exit-on-result --max-ticks 60000 --log $logPath --save-replay $replayPath `
        --hash-trace $tracePath --hash-every 10 --dump $dumpPath 2>&1
    $status = 'UNKNOWN'
    $tick = -1
    $hash = ''
    foreach ($line in $output) {
        $text = [string]$line
        if ($text -match 'status=([A-Z_]+)') { $status = $matches[1] }
        if ($text -match 'tick=(\d+)') { $tick = [int]$matches[1] }
        if ($text -match 'hash=(\d+)') { $hash = $matches[1] }
    }
    $replayOut = & $hostExe --level $run.file --seed $Seed --replay $replayPath --max-ticks 60000 `
        --out (Join-Path $e2Dir ("host_replay_" + $run.id + ".ndjson")) 2>&1
    $replayHash = ''
    $replayTick = -1
    foreach ($line in $replayOut) {
        $text = [string]$line
        if ($text -match 'tick=(-?\d+)') { $replayTick = [int]$matches[1] }
        if ($text -match 'hash=(\d+)') { $replayHash = $matches[1] }
    }
    $ok = ($status -eq 'WIN') -and ($hash -eq $replayHash) -and ($tick -eq $replayTick) -and
        (Test-Path -LiteralPath $dumpPath)
    if (-not $ok) { $autoplayOk = $false }
    $autoplayResults += [ordered]@{ level = $run.id; status = $status; resultTick = $tick;
        hash = $hash; replayTick = $replayTick; replayHash = $replayHash; ok = $ok }
    Write-Host ("AUTOPLAY " + $run.id + " status=" + $status + " tick=" + $tick + " hash=" + $hash +
        " replayHash=" + $replayHash)
}
Add-Step -Name 'autoplay_win' -Ok $autoplayOk -Detail ('levels=' + $levelRuns.Count)

# 6) 采样守卫 A-12：每条折线段的长度 <= 0.25 格 ------------------------------
$samplingOk = $true
$samplingDetail = @()
foreach ($run in $levelRuns) {
    $dumpPath = Join-Path $e2Dir ("final_" + $run.id + ".json")
    if (-not (Test-Path -LiteralPath $dumpPath)) {
        $samplingOk = $false
        $samplingDetail += ("$($run.id):missing")
        continue
    }
    $check = & $python (Join-Path $PSScriptRoot 'check_snapshot.py') $dumpPath 2>&1
    if ($LASTEXITCODE -ne 0) { $samplingOk = $false }
    $samplingDetail += (($check | Select-Object -Last 1) -replace 'SNAPSHOT ', '')
}
Add-Step -Name 'sampling_bound' -Ok $samplingOk -Detail ($samplingDetail -join ' | ')

# 7) A-09：有/无动画客户端时事件流完全一致 ---------------------------------
$animOk = $true
$animDetail = 'no client log'
$clientLog = Join-Path (Get-EvidenceRoot3) 'M25_animation\host.ndjson'
function Get-SessionEvents {
    param([string]$Path)
    $all = @(Get-Content -LiteralPath $Path)
    $lastSession = -1
    for ($i = 0; $i -lt $all.Count; $i++) {
        if ($all[$i] -match '"kind":"SESSION"') { $lastSession = $i }
    }
    if ($lastSession -lt 0) { return @() }
    return @($all[$lastSession..($all.Count - 1)] | Where-Object { $_ -match '"kind":"EVENT"' } |
        ForEach-Object { $o = $_ | ConvertFrom-Json; "$($o.event.seq)|$($o.event.type)|$($o.event.tick)" })
}
if (Test-Path -LiteralPath $clientLog) {
    $clientEvents = @(Get-SessionEvents $clientLog)
    $headlessLog = Join-Path $e2Dir 'host_headless_anim.ndjson'
    $null = & $hostExe --level 'levels/L_CURVE_RING.json' --seed $Seed --autoplay greedy --no-server `
        --max-ticks 3000 --log $headlessLog 2>&1
    $headlessEvents = @(Get-SessionEvents $headlessLog)
    $compare = [Math]::Min($clientEvents.Count, $headlessEvents.Count)
    $mismatches = 0
    for ($i = 0; $i -lt $compare; $i++) {
        if ($clientEvents[$i] -ne $headlessEvents[$i]) { $mismatches++ }
    }
    $animOk = ($compare -gt 20) -and ($mismatches -eq 0)
    $animDetail = "events=$compare mismatches=$mismatches clientLast=$($clientEvents | Select-Object -Last 1)"
    $reportPath = Join-Path (Get-EvidenceRoot3) 'M25_animation\anim_consistency.txt'
    $lines = @(
        'A-09: the client animation layer must not change the logical event stream.',
        ("client log      : " + $clientLog),
        ("headless log    : " + $headlessLog),
        ("events compared : " + $compare),
        ("mismatches      : " + $mismatches),
        ("client last     : " + ($clientEvents | Select-Object -Last 1)),
        ("headless last   : " + ($headlessEvents | Select-Object -Last 1)),
        ("result          : " + $(if ($animOk) { 'PASS' } else { 'FAIL' }))
    )
    Write-Utf8NoBom -Path $reportPath -Content (($lines -join "`r`n") + "`r`n")
}
Add-Step -Name 'animation_consistency' -Ok $animOk -Detail $animDetail

# 8) 视觉证据包齐全 -------------------------------------------------------
$visualOk = $true
$visualDetail = @()
$required = @(
    'M24_client_curve\shot_00_home.png',
    'M24_client_curve\shot_01_curve_initial.png',
    'M24_client_curve\shot_02_curve_collecting.png',
    'M24_client_curve\shot_03_curve_refilled.png',
    'M25_animation\frame_01.png',
    'M25_animation\frame_05.png',
    'M25_animation\frame_09.png',
    'M25_animation\anim_consistency.txt',
    'M26_items\shot_20_items_initial.png',
    'M26_items\shot_21_item_rejected.png',
    'M27_e2e\shot_10_result.png'
)
foreach ($item in $required) {
    $path = Join-Path $evidence $item
    if (-not (Test-Path -LiteralPath $path)) {
        $visualOk = $false
        $visualDetail += ('missing:' + $item)
    } elseif ((Get-Item -LiteralPath $path).Length -le 0) {
        $visualOk = $false
        $visualDetail += ('empty:' + $item)
    }
}
if ($visualDetail.Count -eq 0) { $visualDetail = @('all screenshots present') }
Add-Step -Name 'visual_evidence' -Ok $visualOk -Detail ($visualDetail -join ' ')

# 8b) manifest（其中枚举了每个产物，因此最后写入） --------------------------
$manifestOk = $true
Copy-Item -LiteralPath $testsLog -Destination (Join-Path $e2Dir 'run_all.log') -Force
$manifestCommands = @{
    'M20' = 'powershell -File tools/run_tests.ps1 -Module M20 -NoRegression'
    'M21' = 'powershell -File tools/run_tests.ps1 -Module M21 -NoRegression'
    'M22' = 'powershell -File tools/run_tests.ps1 -Module M22 -NoRegression'
    'M23' = 'powershell -File tools/run_tests.ps1 -Module M23 -NoRegression'
    'M24' = 'powershell -File tools/capture_phase3.ps1 -Preset curve'
    'M25' = 'powershell -File tools/capture_phase3.ps1 -Preset burst -Frames 12'
    'M26' = 'powershell -File tools/capture_phase3.ps1 -Preset items'
    'M27' = 'powershell -File tools/run_all.ps1 -Phase 3'
}
foreach ($module in $moduleRuns + @('M27')) {
    $logName = 'run_all.log'
    $notes = 'No cases skipped. Evidence mapping is documented in Docs/reports/' + $module + '_report.md.'
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'make_manifest.ps1'),
        '-Module', $module, '-Phase', '3', '-LogName', $logName, '-Seed', ([string]$Seed),
        '-BuildVersion', 'phase3-framework', '-Command', $manifestCommands[$module], '-Notes', $notes)
    if ($module -eq 'M26') { $arguments += @('-LevelId', 'L_curve_items') }
    elseif ($module -eq 'M24') { $arguments += @('-LevelId', 'L_CURVE_RING') }
    elseif ($module -eq 'M25') { $arguments += @('-LevelId', 'L_CURVE_RING') }
    elseif ($module -eq 'M27') { $arguments += @('-LevelId', 'L_CURVE_RING') }
    else { $arguments += @('-LevelId', 'L_curve_min') }
    & powershell @arguments *> $null
    $ok = ($LASTEXITCODE -eq 0)
    if (-not $ok) { $manifestOk = $false }
    Write-Host ("MANIFEST " + $module + " ok=" + $ok)
}
Add-Step -Name 'manifests' -Ok $manifestOk -Detail ('modules=' + ($moduleRuns.Count + 1))

# 9) 证据自检 -------------------------------------------------------------
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'verify_evidence.ps1') -All -Phase 3 *> $null
$verifyOk = ($LASTEXITCODE -eq 0)
Add-Step -Name 'verify_evidence' -Ok $verifyOk -Detail 'framework modules'

$durationMs = [int]((Get-Date) - $started).TotalMilliseconds
$buildClean = ($warnings -eq 0) -and ($errors -eq 0)
$result = if ($environmentOk -and $buildOk -and $buildClean -and $testsOk -and $modulesOk -and
    $autoplayOk -and $samplingOk -and $animOk -and $visualOk -and $verifyOk) { 'PASS' } else { 'FAIL' }

$phaseSummary = [ordered]@{
    generatedAt  = (Get-IsoTimestamp)
    phase        = 3
    seed         = $Seed
    result       = $result
    durationMs   = $durationMs
    caseCount    = if ($summary) { [int]$summary.cases } else { 0 }
    passCount    = if ($summary) { [int]$summary.pass } else { 0 }
    failCount    = if ($summary) { [int]$summary.fail } else { 0 }
    skippedCount = if ($summary) { [int]$summary.skipped } else { 0 }
    build        = [ordered]@{ warnings = $warnings; errors = $errors; clean = [bool]$CleanBuild }
    steps        = $steps
    autoplay     = $autoplayResults
    sampling     = $samplingDetail
    animation    = $animDetail
    artifacts    = @('phase3_run_all.log', 'phase3_summary.json', 'M27_e2e/host_win_L_CURVE_RING.ndjson',
        'M27_e2e/final_L_CURVE_RING.json', 'M25_animation/anim_consistency.txt')
}
$summaryPath = Join-Path $evidence 'phase3_summary.json'
Write-Utf8NoBom -Path $summaryPath -Content ($phaseSummary | ConvertTo-Json -Depth 8)
Write-Host ('PHASE3_SUMMARY ' + $summaryPath)
Write-Host ('PHASE3_RESULT ' + $result)
if ($result -ne 'PASS') { exit 1 }
exit 0
