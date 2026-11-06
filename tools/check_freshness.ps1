# 证据新鲜度检查：每个产物都必须比它所覆盖的代码更新。
param(
    [string]$Module,
    [switch]$All
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$sourcePaths = @('core', 'host', 'tests', 'assets\scripts', 'CMakeLists.txt')
$newestSource = [datetime]::MinValue
$newestFile = ''
foreach ($relative in $sourcePaths) {
    $path = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $path)) { continue }
    $item = Get-Item -LiteralPath $path
    $files = @()
    if ($item.PSIsContainer) {
        $files = @(Get-ChildItem -LiteralPath $path -Recurse -File -ErrorAction SilentlyContinue)
    } else {
        $files = @($item)
    }
    foreach ($file in $files) {
        if ($file.LastWriteTime -gt $newestSource) {
            $newestSource = $file.LastWriteTime
            $newestFile = $file.FullName
        }
    }
}
Write-Output ('NEWEST_SOURCE ' + $newestSource.ToString('yyyy-MM-dd HH:mm:ss') + ' ' + $newestFile)

$modules = @()
if ($All) {
    foreach ($key in $script:ModuleDirectoryMap.Keys) { $modules += $key }
} elseif ($Module) {
    $modules += $Module
} else {
    Write-Output 'usage: check_freshness.ps1 -Module M5 | -All'
    exit 2
}

$stale = @()
foreach ($moduleId in $modules) {
    $dir = Get-ModuleEvidenceDirectory $moduleId
    if (-not (Test-Path -LiteralPath $dir)) {
        $stale += ($moduleId + ': directory missing')
        continue
    }
    $artifacts = @(Get-ChildItem -LiteralPath $dir -File | Where-Object { $_.Name -ne 'run_manifest.json' })
    $oldest = [datetime]::MaxValue
    $oldestName = ''
    foreach ($artifact in $artifacts) {
        if ($artifact.LastWriteTime -lt $oldest) {
            $oldest = $artifact.LastWriteTime
            $oldestName = $artifact.Name
        }
    }
    if ($oldest -eq [datetime]::MaxValue) {
        $stale += ($moduleId + ': no artifacts')
        continue
    }
    $ok = $oldest -ge $newestSource
    Write-Output ("{0} oldest={1:yyyy-MM-dd HH:mm:ss} ({2}) fresh={3}" -f $moduleId, $oldest, $oldestName, $ok)
    if (-not $ok) {
        $stale += ($moduleId + ': ' + $oldestName)
    }
}

if ($stale.Count -gt 0) {
    Write-Output ('STALE_EVIDENCE count=' + $stale.Count)
    foreach ($item in $stale) { Write-Output (' - ' + $item) }
    exit 1
}
Write-Output ('FRESHNESS_OK modules=' + $modules.Count)
exit 0
