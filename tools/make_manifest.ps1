# 依据运行日志生成 Docs/evidence/phase1/M{nn}_{slug}/run_manifest.json。
param(
    [Parameter(Mandatory = $true)][string]$Module,
    [Parameter(Mandatory = $true)][string]$Command,
    [string]$Seed = '20261104',
    [string]$LevelId = 'L01_default',
    [string]$Result,
    [string]$BuildVersion = 'phase1-m9.1',
    [string]$Notes = 'No cases skipped. See the module report for the acceptance mapping.',
    [string]$LogName = 'run_all.log',
    [int]$Phase = 1
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$directory = Get-ModuleEvidenceDirectory -Module $Module -Phase $Phase
if (-not (Test-Path -LiteralPath $directory)) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

$logPath = Join-Path $directory $LogName
if (-not (Test-Path -LiteralPath $logPath)) {
    Write-Output ('MANIFEST_FAILED missing log ' + $logPath)
    exit 1
}

$lines = @(Get-Content -LiteralPath $logPath | Where-Object { $_.Trim().Length -gt 0 })
if ($lines.Count -eq 0) {
    Write-Output 'MANIFEST_FAILED empty log'
    exit 1
}
$summaryLine = $lines | Where-Object { $_ -match '"kind":"SUMMARY"' } | Select-Object -Last 1
if (-not $summaryLine) {
    Write-Output 'MANIFEST_FAILED no SUMMARY line in log'
    exit 1
}
$summary = $summaryLine | ConvertFrom-Json

if (-not $Result) { $Result = [string]$summary.result }

$environmentPath = Join-Path (Get-EvidenceRoot -Phase 1) 'M00_harness\environment.json'
$cmake = 'unknown'
$msvc = 'unknown'
$cocos = 'unknown'
if (Test-Path -LiteralPath $environmentPath) {
    $environment = Read-JsonFile $environmentPath
    $cmake = [string]$environment.cmake
    $msvc = [string]$environment.compiler
    $cocos = [string]$environment.cocos
}

$gitCommit = 'uncommitted-worktree'
try {
    $value = & git -C $root rev-parse --short HEAD 2>$null
    if ($LASTEXITCODE -eq 0 -and $value) { $gitCommit = ([string]$value).Trim() }
} catch { $gitCommit = 'uncommitted-worktree' }
$dirty = & git -C $root status --porcelain 2>$null
if ($dirty) { $gitCommit = $gitCommit + '+dirty' }

$artifacts = @()
foreach ($item in (Get-ChildItem -LiteralPath $directory -File | Sort-Object Name)) {
    if ($item.Name -eq 'run_manifest.json') { continue }
    $artifacts += $item.Name
}

$manifest = [ordered]@{
    module      = $Module
    title       = (Get-ModuleTitle $Module)
    generatedAt = (Get-IsoTimestamp)
    operator    = 'AI Agent'
    commit      = $gitCommit
    buildVersion = $BuildVersion
    toolchain   = [ordered]@{ cmake = $cmake; msvc = $msvc; cocos = $cocos }
    command     = $Command
    seed        = $Seed
    levelId     = $LevelId
    environment = 'Docs/evidence/phase1/M00_harness/environment.json'
    result      = $Result
    caseCount   = [int]$summary.cases
    passCount   = [int]$summary.pass
    failCount   = [int]$summary.fail
    artifacts   = $artifacts
    notes       = $Notes
}

$json = $manifest | ConvertTo-Json -Depth 6
Write-Utf8NoBom -Path (Join-Path $directory 'run_manifest.json') -Content $json
Write-Output ('MANIFEST ' + (Join-Path $directory 'run_manifest.json') + ' cases=' + $manifest.caseCount + ' result=' + $manifest.result)
exit 0
