# 运行 fc_tests 的模块回归，并写出 NDJSON 日志与逐用例日志。
param(
    [string]$Module,
    [string]$Case,
    [int]$Seed = 20261104,
    [string]$Log,
    [switch]$All,
    [switch]$NoRegression,
    [string]$OutDir
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$exe = Get-BinaryPath -Name 'fc_tests'
if (-not $exe) {
    Write-Output 'TESTS_FAILED fc_tests.exe not found - run tools/build.ps1 first'
    exit 1
}

if (-not $Log) {
    $Log = Join-Path $root 'temp\run_all.log'
}
$Log = Resolve-FullPath $Log

if (-not $OutDir) {
    $OutDir = Join-Path $root 'temp\percase'
}
$OutDir = Resolve-FullPath $OutDir
if (Test-Path -LiteralPath $OutDir) {
    Get-ChildItem -LiteralPath $OutDir -Filter '*.log' -ErrorAction SilentlyContinue | Remove-Item -Force
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$arguments = @('--seed', $Seed, '--out', $Log, '--outdir', $OutDir, '--verbose')

if ($Case) {
    $arguments += @('--case', $Case)
} elseif ($All) {
    $arguments += @('--modules', 'M0,M1,M2,M3,M4,M5,M6,M7,M8,M9,M12,M13,M14,M15,M16,M17,M18,M20,M21,M22,M23,M26')
} elseif ($Module) {
    $moduleNumber = 0
    if ($Module -notmatch '^M(\d+)$') {
        Write-Output ("TESTS_FAILED unknown module '" + $Module + "'")
        exit 2
    }
    $moduleNumber = [int]$matches[1]
    if ($NoRegression) {
        $arguments += @('--module', $Module)
    } else {
        $list = @()
        for ($i = 0; $i -le $moduleNumber; $i++) { $list += ('M' + $i) }
        $arguments += @('--modules', ($list -join ','))
    }
}

$output = & $exe @arguments 2>&1
$exitCode = $LASTEXITCODE
foreach ($line in $output) { Write-Output ([string]$line) }

if (-not (Test-Path -LiteralPath $Log)) {
    Write-Output 'TESTS_FAILED log file missing'
    exit 1
}

$lines = Get-Content -LiteralPath $Log
$summaryLine = $lines | Where-Object { $_ -match '"kind":"SUMMARY"' } | Select-Object -Last 1
if (-not $summaryLine) {
    Write-Output 'TESTS_FAILED no SUMMARY line'
    exit 1
}
$summary = $summaryLine | ConvertFrom-Json

# 把与证据相关的逐用例日志复制到运行日志同级目录。
if ($Module) {
    $evidenceDir = Split-Path -Parent $Log
    if (-not (Test-Path -LiteralPath $evidenceDir)) {
        New-Item -ItemType Directory -Force -Path $evidenceDir | Out-Null
    }
    $prefix = 'TC-' + $Module + '-'
    $copied = 0
    foreach ($file in (Get-ChildItem -LiteralPath $OutDir -Filter '*.log')) {
        if ($file.Name.StartsWith($prefix)) {
            Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $evidenceDir $file.Name) -Force
            $copied++
        }
    }
    Write-Output ('CASE_LOGS_COPIED ' + $copied + ' -> ' + $evidenceDir)
}

Write-Output ('SUMMARY cases=' + $summary.cases + ' pass=' + $summary.pass + ' fail=' + $summary.fail + ' skipped=' + $summary.skipped + ' result=' + $summary.result)
Write-Output ('LOG ' + $Log)

if ($summary.fail -ne 0 -or $summary.skipped -ne 0 -or $exitCode -ne 0) {
    exit 1
}
exit 0
