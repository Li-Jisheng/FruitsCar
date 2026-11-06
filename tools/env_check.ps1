# 收集构建/运行环境信息写入 environment.json（M0 / A5）。
param(
    [string]$OutPath
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
if (-not $OutPath) {
    $OutPath = Join-Path (Get-EvidenceRoot) 'M00_harness\environment.json'
}
$OutPath = Resolve-FullPath $OutPath

$cmake = Get-CMakePath
$cmakeVersion = (& $cmake --version | Select-Object -First 1)
$ninja = Get-NinjaPath
$ninjaVersion = ''
if ($ninja) { $ninjaVersion = (& $ninja --version) }

$msvc = $null
$compiler = 'unknown'
try {
    $msvc = Import-MsvcEnvironment
    $clPath = (Get-Command cl.exe).Source
    $clText = ''
    try {
        $clText = (cmd /c "cl 2>&1" | Out-String)
    } catch {
        $clText = ''
    }
    if ($clText -match '(\d+\.\d+\.\d+\.?\d*)') {
        $compiler = 'MSVC ' + $matches[1]
    } else {
        $compiler = 'MSVC (cl.exe present at ' + $clPath + ')'
    }
} catch {
    $compiler = 'MSVC unavailable: ' + $_.Exception.Message
}

function Test-PortFree {
    param([int]$Port)
    $listener = $null
    try {
        $listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, $Port)
        $listener.Start()
        return $true
    } catch {
        return $false
    } finally {
        if ($listener) { $listener.Stop() }
    }
}

$projectPackage = Read-JsonFile (Join-Path $root 'package.json')
$cocosVersion = $projectPackage.creator.version

$nodeVersion = ''
try { $nodeVersion = (& node -v) } catch { $nodeVersion = 'unavailable' }

$mcpProfile = 'unavailable'
$mcpPort = 0
$mcpTools = 0
try {
    $infoPath = Join-Path $root 'temp\mcp_project_info.json'
    $null = powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'mcp_call.ps1') -Tool 'get_project_info' -Out $infoPath 2>&1
    if (Test-Path -LiteralPath $infoPath) {
        $info = Read-JsonFile $infoPath
        $mcpProfile = [string]$info.data.config.toolProfile
        $mcpPort = [int]$info.data.config.port
    }
} catch {
    $mcpProfile = 'unavailable'
}

$toolCatalogPath = Join-Path $root 'temp\mcp_tools.json'
try {
    $init = @{ protocolVersion = '2025-06-18'; capabilities = @{}; clientInfo = @{ name = 'env_check'; version = '1.0' } }
    $initFile = Join-Path $root 'temp\mcp_init.json'
    Write-Utf8NoBom -Path $initFile -Content ($init | ConvertTo-Json -Depth 5)
    $response = powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'mcp_request.ps1') -Method 'tools/list' -ParamsFile $initFile 2>&1
    if ($response) {
        $parsed = ($response | Out-String) | ConvertFrom-Json
        if ($parsed.result.tools) {
            $mcpTools = @($parsed.result.tools).Count
            Write-Utf8NoBom -Path $toolCatalogPath -Content (($parsed.result.tools | ForEach-Object { $_.name }) -join "`n")
        }
    }
} catch {
    $mcpTools = 0
}

$gitCommit = 'uncommitted-worktree'
try {
    $gitValue = & git -C $root rev-parse --short HEAD 2>$null
    if ($LASTEXITCODE -eq 0 -and $gitValue) {
        $gitCommit = ([string]$gitValue).Trim()
    }
} catch {
    $gitCommit = 'uncommitted-worktree'
}

$environment = [ordered]@{
    generatedAt      = (Get-IsoTimestamp)
    os               = [System.Environment]::OSVersion.VersionString
    osArchitecture   = $env:PROCESSOR_ARCHITECTURE
    machineName      = $env:COMPUTERNAME
    powershell       = [string]$PSVersionTable.PSVersion
    pwshAvailable    = [bool](Get-Command pwsh -ErrorAction SilentlyContinue)
    cmake            = [string]$cmakeVersion
    cmakePath        = $cmake
    ninja            = [string]$ninjaVersion
    compiler         = $compiler
    msvcPath         = $msvc
    cocos            = $cocosVersion
    node             = [string]$nodeVersion
    mcpProfile       = $mcpProfile
    mcpPort          = $mcpPort
    mcpToolCount     = $mcpTools
    portFree         = (Test-PortFree -Port 8787)
    port             = 8787
    projectRoot      = $root
    gitCommit        = $gitCommit
}

$json = $environment | ConvertTo-Json -Depth 6
Write-Utf8NoBom -Path $OutPath -Content $json
$tempCopy = Join-Path $root 'temp\environment.json'
Write-Utf8NoBom -Path $tempCopy -Content $json
Write-Output ('ENVIRONMENT ' + $OutPath)
Write-Output $json
$required = @('os', 'cmake', 'compiler', 'cocos', 'node', 'mcpProfile', 'portFree', 'generatedAt')
$missing = @()
foreach ($field in $required) {
    if (-not $environment.Contains($field) -or $null -eq $environment[$field] -or [string]$environment[$field] -eq '') {
        $missing += $field
    }
}
if ($missing.Count -gt 0) {
    Write-Output ('ENVIRONMENT_INCOMPLETE missing=' + ($missing -join ','))
    exit 1
}
exit 0
