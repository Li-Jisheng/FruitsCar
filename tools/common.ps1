# FruitsCar phase-1 工具脚本的共享辅助函数。
# 注意：本文件保存为 UTF-8 BOM 编码——PowerShell 5.1 在没有 BOM 时按 ANSI 读取 .ps1，中文会破坏语法。
$ErrorActionPreference = 'Stop'

function Get-ProjectRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

function Get-VsWherePath {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    return $null
}

function Get-MsvcInstallPath {
    $vswhere = Get-VsWherePath
    if (-not $vswhere) {
        return $null
    }
    $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($path) {
        return ($path | Select-Object -First 1).Trim()
    }
    return $null
}

# 将 MSVC 环境导入当前 PowerShell 会话。
function Import-MsvcEnvironment {
    $install = Get-MsvcInstallPath
    if (-not $install) {
        throw 'Visual Studio with the C++ toolset was not found (vswhere reported nothing).'
    }
    $vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcvars)) {
        throw "vcvars64.bat not found at $vcvars"
    }
    $lines = & cmd.exe /c "`"$vcvars`" >nul 2>&1 && set"
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') {
            $name = $matches[1]
            $value = $matches[2]
            Set-Item -Path ("Env:" + $name) -Value $value -ErrorAction SilentlyContinue
        }
    }
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw 'cl.exe was not found on PATH after importing the MSVC environment.'
    }
    return $install
}

function Get-CMakePath {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    foreach ($candidate in @('C:\Program Files\CMake\bin\cmake.exe', 'D:\Software\CMake\bin\cmake.exe')) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    throw 'cmake.exe was not found on PATH.'
}

function Get-NinjaPath {
    $command = Get-Command ninja -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    return $null
}

function Get-BuildDirectory {
    $root = Get-ProjectRoot
    return (Join-Path $root 'temp\build')
}

function Get-CMakeGeneratorArgs {
    $ninja = Get-NinjaPath
    if ($ninja) {
        return @('-G', 'Ninja')
    }
    return @('-G', 'Visual Studio 17 2022', '-A', 'x64')
}

function Get-BinaryPath {
    param([string]$Name)
    $dir = Get-BuildDirectory
    foreach ($candidate in @(
            (Join-Path $dir ($Name + '.exe')),
            (Join-Path $dir ('Release\' + $Name + '.exe')),
            (Join-Path $dir ('Debug\' + $Name + '.exe')),
            (Join-Path $dir ('RelWithDebInfo\' + $Name + '.exe')))) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    return $null
}

function Write-Utf8NoBom {
    param([string]$Path, [string]$Content)
    $directory = Split-Path -Parent $Path
    if ($directory -and -not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $encoding)
}

function Read-JsonFile {
    param([string]$Path)
    $text = [System.IO.File]::ReadAllText($Path)
    return ($text | ConvertFrom-Json)
}

function Resolve-FullPath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $Path))
}

function Get-IsoTimestamp {
    return (Get-Date).ToString('yyyy-MM-ddTHH:mm:sszzz')
}

$script:ModuleDirectoryMap = [ordered]@{
    'M0'  = 'M00_harness'
    'M1'  = 'M01_level_model'
    'M2'  = 'M02_fruit_gen'
    'M3'  = 'M03_parking'
    'M4'  = 'M04_dispatch'
    'M5'  = 'M05_collect_rules'
    'M6'  = 'M06_refill'
    'M7'  = 'M07_routing'
    'M8'  = 'M08_result'
    'M9'  = 'M09_extensibility'
    'M10' = 'M10_client_view'
    'M11' = 'M11_e2e'
}

# Phase 2 模块（M12..M19）。
$script:Phase2ModuleDirectoryMap = [ordered]@{
    'M12' = 'M12_level_model'
    'M13' = 'M13_contacts'
    'M14' = 'M14_dual_lane'
    'M15' = 'M15_refill'
    'M16' = 'M16_level1'
    'M17' = 'M17_level2'
    'M18' = 'M18_home'
    'M19' = 'M19_e2e'
}

# Phase 3（框架演进）模块（M20..M27）。
$script:Phase3ModuleDirectoryMap = [ordered]@{
    'M20' = 'M20_geom'
    'M21' = 'M21_world'
    'M22' = 'M22_level_v2'
    'M23' = 'M23_collect_refill'
    'M24' = 'M24_client_curve'
    'M25' = 'M25_animation'
    'M26' = 'M26_items'
    'M27' = 'M27_e2e'
}

function Get-EvidenceRoot {
    param([int]$Phase = 1)
    $root = Get-ProjectRoot
    if ($Phase -eq 3) {
        return (Join-Path $root 'Docs\evidence\framework')
    }
    if ($Phase -eq 2) {
        return (Join-Path $root 'Docs\evidence\phase2')
    }
    return (Join-Path $root 'Docs\evidence\phase1')
}

function Get-EvidenceRoot2 {
    return (Join-Path (Get-ProjectRoot) 'Docs\evidence\phase2')
}

function Get-EvidenceRoot3 {
    return (Join-Path (Get-ProjectRoot) 'Docs\evidence\framework')
}

function Get-ModuleEvidenceDirectory {
    param([string]$Module, [int]$Phase = 1)
    if ($Phase -eq 3) {
        if (-not $script:Phase3ModuleDirectoryMap.Contains($Module)) {
            throw "unknown phase-3 module '$Module'"
        }
        return (Join-Path (Get-EvidenceRoot3) $script:Phase3ModuleDirectoryMap[$Module])
    }
    if ($Phase -eq 2) {
        if (-not $script:Phase2ModuleDirectoryMap.Contains($Module)) {
            throw "unknown phase-2 module '$Module'"
        }
        return (Join-Path (Get-EvidenceRoot2) $script:Phase2ModuleDirectoryMap[$Module])
    }
    if (-not $script:ModuleDirectoryMap.Contains($Module)) {
        throw "unknown module '$Module'"
    }
    return (Join-Path (Get-EvidenceRoot) $script:ModuleDirectoryMap[$Module])
}

function Get-ModuleTitle {
    param([string]$Module)
    $titles = @{
        'M0'  = 'Engineering skeleton and verifiable environment'
        'M1'  = 'Level configuration and domain model'
        'M2'  = 'Fruit area initialisation and generation'
        'M3'  = 'Parking lanes and vehicle queues'
        'M4'  = 'Dispatch arbitration and 0.5s throttle'
        'M5'  = 'Road driving and fruit collection'
        'M6'  = 'Fruit refill and settle cooldown'
        'M7'  = 'Road-end routing and parking'
        'M8'  = 'Win / jam resolution and terminal lock'
        'M9'  = 'Level extensibility verification'
        'M10' = 'Cocos client presentation layer'
        'M11' = 'End-to-end integration and acceptance'
        'M12' = 'Level data model v2 and loading-time validation'
        'M13' = 'Contact rules: sides, bend complement, lane distance'
        'M14' = 'Two lanes on one path and alternating dispatch'
        'M15' = 'layerIn and regionIn refill'
        'M16' = 'Level 1 ring orchard: data, rules and presentation'
        'M17' = 'Level 2 zigzag wide: data, rules and presentation'
        'M18' = 'Home screen, level catalogue and navigation'
        'M19' = 'End-to-end evidence and full regression'
        'M20' = 'Curve geometry kernel and golden hashes'
        'M21' = 'World abstraction (GridWorld / CurveWorld)'
        'M22' = 'Level schema v2, curve validation and new level data'
        'M23' = 'Arc-length contact, layer-chain refill and conservation'
        'M24' = 'Curve road and free fruit layout (client, E3)'
        'M25' = 'Client animation layer (E3 frame sequence)'
        'M26' = 'Reserved skill interfaces (items / use_item)'
        'M27' = 'Framework end-to-end evidence and full regression'
    }
    return $titles[$Module]
}
