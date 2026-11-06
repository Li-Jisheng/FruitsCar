# 重新生成各模块的证据目录（M0..M9）、M0 的负向
# 检查以及 M11 端到端 host 运行，最后写入运行 manifest。
param(
    [int]$Seed = 20261104
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$evidence = Get-EvidenceRoot
$testsExe = Get-BinaryPath -Name 'fc_tests'
$hostExe = Get-BinaryPath -Name 'fc_host'
if (-not $testsExe -or -not $hostExe) {
    Write-Output 'GEN_EVIDENCE_FAILED build first'
    exit 1
}

$moduleLevel = @{
    'M0' = 'L01_default'; 'M1' = 'L01_default'; 'M2' = 'L01_default'; 'M3' = 'L01_default'
    'M4' = 'L01_default'; 'M5' = 'L01_default'; 'M6' = 'L_refill';    'M7' = 'L_route'
    'M8' = 'L01_default'; 'M9' = 'L02_8x4,L03_vertical,L04_wide0,L04_wide1,L05_ring,L06_bent'
}

foreach ($module in @('M0', 'M1', 'M2', 'M3', 'M4', 'M5', 'M6', 'M7', 'M8', 'M9')) {
    $directory = Get-ModuleEvidenceDirectory $module
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }
    $logPath = Join-Path $directory 'run_all.log'
    $command = 'powershell -NoProfile -File tools/run_tests.ps1 -Module ' + $module + ' -Seed ' + $Seed +
               ' -Log Docs/evidence/phase1/' + (Split-Path -Leaf $directory) + '/run_all.log'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run_tests.ps1') `
        -Module $module -Seed $Seed -Log $logPath *> $null
    $ok = ($LASTEXITCODE -eq 0)
    Write-Output ("GEN " + $module + " tests=" + $ok)
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'make_manifest.ps1') `
        -Module $module -Command $command -Seed $Seed -LevelId $moduleLevel[$module] *> $null
}

# --- M0 负向对照 ----------------------------------------------------------
$m0 = Get-ModuleEvidenceDirectory 'M0'
$injectionLog = Join-Path $m0 'TC-M0-01_fail_injection.log'
& $testsExe --inject-failing-case --out $injectionLog *> $null
$injectionExit = $LASTEXITCODE
Write-Output ("GEN TC-M0-01 exit=" + $injectionExit)

$negative = Join-Path $m0 'verify_evidence_negative.txt'
$fakeDir = Join-Path $root 'temp\evidence_negative_sample'
if (Test-Path -LiteralPath $fakeDir) { Remove-Item -LiteralPath $fakeDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $fakeDir | Out-Null
Set-Content -Path (Join-Path $fakeDir 'run_all.log') -Value '{"kind":"SUMMARY","result":"PASS"}' -Encoding ASCII
$negativeText = 'verify_evidence.ps1 against a directory without run_manifest.json' + "`r`n"
$negativeText += (& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'verify_evidence.ps1') -Module M0 -Quiet 2>&1 | Out-String)
Write-Utf8NoBom -Path $negative -Content $negativeText
Remove-Item -LiteralPath $fakeDir -Recurse -Force -ErrorAction SilentlyContinue

# 构建日志和 environment.json 由 tools/build.ps1 与 env_check.ps1 生成；
# 最后再刷新 M0 的 manifest，以便把负向对照的产物列进去。
$commandM0 = 'powershell -NoProfile -File tools/run_tests.ps1 -Module M0 -Seed ' + $Seed +
             ' -Log Docs/evidence/phase1/M00_harness/run_all.log'
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'make_manifest.ps1') `
    -Module 'M0' -Command $commandM0 -Seed $Seed -LevelId 'L01_default' *> $null

# --- M11 端到端 host 运行 -------------------------------------------------
$e2e = Get-ModuleEvidenceDirectory 'M11'
if (-not (Test-Path -LiteralPath $e2e)) { New-Item -ItemType Directory -Force -Path $e2e | Out-Null }
$runs = @(
    @{ name = 'win_20261104'; level = 'tests/levels/L01_default.json'; seed = 20261104; mode = 'greedy' },
    @{ name = 'win_7';        level = 'tests/levels/L01_default.json'; seed = 7;        mode = 'greedy' },
    @{ name = 'win_42';       level = 'tests/levels/L01_default.json'; seed = 42;       mode = 'greedy' },
    @{ name = 'jam_20261104'; level = 'tests/levels/L_jam.json';       seed = 20261104; mode = 'spam' }
)
$lines = @()
foreach ($run in $runs) {
    $logPath = Join-Path $e2e ('host_' + $run.name + '.ndjson')
    $replayPath = Join-Path $e2e ('replay_' + $run.name + '.jsonl')
    $output = & $hostExe --level $run.level --seed $run.seed --autoplay $run.mode --no-server `
        --exit-on-result --max-ticks 40000 --log $logPath --save-replay $replayPath 2>&1
    $text = ($output | Out-String).Trim()
    $lines += ($run.name + ': ' + $text)
    Write-Output ("E2E " + $run.name + " " + $text)
}
$lines | Set-Content -Path (Join-Path $e2e 'e2e_summary.txt') -Encoding UTF8

# 完整回归日志也会放在 E2E 证据旁边。
$combined = Join-Path $evidence 'phase1_run_all.log'
if (-not (Test-Path -LiteralPath $combined)) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run_tests.ps1') `
        -All -Seed $Seed -Log $combined *> $null
}
Copy-Item -LiteralPath $combined -Destination (Join-Path $e2e 'TC-M11-06_regression.log') -Force

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'make_manifest.ps1') `
    -Module M11 -Command 'powershell -NoProfile -File tools/run_all.ps1' -Seed $Seed `
    -LevelId 'L01_default,L_jam' -LogName 'TC-M11-06_regression.log' *> $null

Write-Output 'GEN_EVIDENCE_DONE'
exit 0
