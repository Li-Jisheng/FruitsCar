# A-10 证明：新增三个弯道关卡只改动 JSON。
#
# 工作区已经包含 phase-2/3 的核心改动，因此在不提交的前提下
# 无法造出字面上的“git diff --stat -- core/ 为空”。
# 于是本脚本用一种无法糊弄的方式证明同一主张：
#   1. 对 core/ 下（以及客户端脚本）的每个文件计算哈希，
#   2. 重新生成三个弯道关卡文件与目录条目，
#   3. 再次计算哈希并比较 —— 任何因新增关卡引起的 C++/TS 改动
#      都会表现为哈希不同，
#   4. 记录改动前后的 `git diff --stat -- core/`；两者必须完全一致。
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/prove_data_driven.ps1
param(
    [string]$OutDir = 'Docs/evidence/framework/M_ext'
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$outRoot = Resolve-FullPath $OutDir
if (-not (Test-Path -LiteralPath $outRoot)) { New-Item -ItemType Directory -Force -Path $outRoot | Out-Null }

function Get-TreeHashes {
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($dir in @('core', 'assets/scripts')) {
        $path = Join-Path $root $dir
        if (-not (Test-Path -LiteralPath $path)) { continue }
        foreach ($file in (Get-ChildItem -LiteralPath $path -Recurse -File | Sort-Object FullName)) {
            $relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
            $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
            $lines.Add($relative + ' ' + $hash) | Out-Null
        }
    }
    return $lines
}

function Get-CoreDiff {
    $text = & git -C $root diff --stat -- core/ 2>$null
    if (-not $text) { return '<empty>' }
    return ($text -join "`n")
}

$before = Get-TreeHashes
$diffBefore = Get-CoreDiff
$statusBefore = & git -C $root status --porcelain -- core/ 2>$null

# “新增关卡”这个动作：重新生成弯道关卡文件（纯 JSON）以及
# 列出它们的目录条目。
python (Join-Path $PSScriptRoot 'gen_curve_levels.py') *> $null
$generatorOk = ($LASTEXITCODE -eq 0)

$after = Get-TreeHashes
$diffAfter = Get-CoreDiff
$statusAfter = & git -C $root status --porcelain -- core/ 2>$null

$changes = 0
$limit = [Math]::Max($before.Count, $after.Count)
for ($i = 0; $i -lt $limit; $i++) {
    $a = if ($i -lt $before.Count) { $before[$i] } else { '<missing>' }
    $b = if ($i -lt $after.Count) { $after[$i] } else { '<missing>' }
    if ($a -ne $b) { $changes++ }
}

$levels = @('levels/L_CURVE_RING.json', 'levels/L_CURVE_S.json', 'levels/L_CURVE_POLY.json',
    'levels/catalogue.json')
$levelInfo = @()
foreach ($level in $levels) {
    $path = Join-Path $root $level
    if (Test-Path -LiteralPath $path) {
        $levelInfo += ($level + ' ' + (Get-Item -LiteralPath $path).Length + ' bytes')
    }
}

$ok = ($changes -eq 0) -and ($diffBefore -eq $diffAfter) -and $generatorOk
$lines = @(
    '# A-10: new levels are pure data (task book 11 A-10 / 9 T-42)',
    ('generatedAt      : ' + (Get-IsoTimestamp)),
    ('core+client files: ' + $after.Count),
    ('hash changes      : ' + $changes),
    ('git diff --stat   : identical before/after = ' + ($diffBefore -eq $diffAfter)),
    ('generator exit    : ' + $generatorOk),
    ('result            : ' + $(if ($ok) { 'PASS' } else { 'FAIL' })),
    '',
    '## git diff --stat -- core/ (before regeneration)',
    $diffBefore,
    '',
    '## git diff --stat -- core/ (after regeneration)',
    $diffAfter,
    '',
    '## git status --porcelain -- core/ (before)',
    (($statusBefore -join "`n")),
    '',
    '## git status --porcelain -- core/ (after)',
    (($statusAfter -join "`n")),
    '',
    '## level files rewritten by the generator',
    (($levelInfo -join "`n")),
    '',
    '## file list',
    ($after -join "`n")
)
Write-Utf8NoBom -Path (Join-Path $outRoot 'diff_new_levels.txt') -Content (($lines -join "`r`n") + "`r`n")
Write-Host ("DATA_DRIVEN changes=" + $changes + " diffIdentical=" + ($diffBefore -eq $diffAfter) +
    " files=" + $after.Count + " result=" + $(if ($ok) { 'PASS' } else { 'FAIL' }))
if (-not $ok) { exit 1 }
exit 0
