# 一键 Release 构建 fc_core / fc_host / fc_tests，并把警告当作错误处理。
param(
    [string]$Config = 'Release',
    [switch]$Clean,
    [string]$LogPath
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$buildDir = Get-BuildDirectory
$log = $LogPath
if (-not $log) {
    $log = Join-Path $root 'Docs\evidence\phase1\M00_harness\build.log'
}

$cmake = Get-CMakePath
$started = Get-Date
$transcript = New-Object System.Collections.Generic.List[string]

try {
    $install = Import-MsvcEnvironment
} catch {
    Write-Output ("BUILD_FAILED " + $_.Exception.Message)
    exit 1
}

if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
    Remove-Item -LiteralPath $buildDir -Recurse -Force
}
if (-not (Test-Path -LiteralPath $buildDir)) {
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
}

$generatorArgs = Get-CMakeGeneratorArgs
$configureArgs = @('-S', $root, '-B', $buildDir) + $generatorArgs + @('-DCMAKE_BUILD_TYPE=' + $Config) +
    @('-DCMAKE_EXPORT_COMPILE_COMMANDS=ON')
$transcript.Add('$ cmake ' + ($configureArgs -join ' '))
$configureOutput = & $cmake @configureArgs 2>&1
$configureExit = $LASTEXITCODE
foreach ($line in $configureOutput) {
    $text = [string]$line
    if ($text -notmatch 'including file|包含文件') { $transcript.Add($text) }
}
if ($configureExit -ne 0) {
    $transcript.Add('CONFIGURE_FAILED exit=' + $configureExit)
    Write-Utf8NoBom -Path $log -Content (($transcript -join "`r`n") + "`r`n")
    Write-Output ('BUILD_FAILED configure exit=' + $configureExit)
    exit $configureExit
}

$buildArgs = @('--build', $buildDir, '--config', $Config)
$transcript.Add('$ cmake ' + ($buildArgs -join ' '))
$buildOutput = & $cmake @buildArgs 2>&1
$buildExit = $LASTEXITCODE
foreach ($line in $buildOutput) {
    $text = [string]$line
    if ($text -match 'including file|包含文件') { continue }
    $transcript.Add($text)
}

$warnings = ($transcript | Where-Object { $_ -match 'warning C|warning:' }).Count
$errors = ($transcript | Where-Object { $_ -match 'error C|error LNK|error MSB' }).Count

$targets = @()
foreach ($name in @('fc_core.lib', 'fc_host.exe', 'fc_tests.exe')) {
    $found = Get-ChildItem -Path $buildDir -Recurse -Filter $name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        $targets += ('{0} ({1} bytes)' -f $name, $found.Length)
    }
}
$transcript.Add('')
$transcript.Add('TARGETS: ' + ($targets -join '; '))
$transcript.Add(('WARNINGS=' + $warnings + ' ERRORS=' + $errors + ' EXIT=' + $buildExit))
$transcript.Add(('DURATION_MS=' + [int]((Get-Date) - $started).TotalMilliseconds))
$transcript.Add(('MSVC=' + $install))

Write-Utf8NoBom -Path $log -Content (($transcript -join "`r`n") + "`r`n")
foreach ($line in $transcript) { Write-Output $line }

if ($buildExit -ne 0) {
    Write-Output ('BUILD_FAILED exit=' + $buildExit)
    exit $buildExit
}
if ($warnings -gt 0) {
    Write-Output ('BUILD_FAILED_WARNINGS warnings=' + $warnings)
    exit 1
}
Write-Output ('BUILD_OK targets=' + $targets.Count + ' warnings=0')
exit 0
