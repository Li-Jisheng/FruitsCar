# 通过 Funplay 的 streamable-HTTP 端点调用一个 MCP 工具，并打印
# 文本载荷（可选把图像载荷保存到磁盘）。
param(
    [Parameter(Mandatory = $true)][string]$Tool,
    [string]$ArgumentsFile,
    [string]$Uri = 'http://127.0.0.1:8765/mcp',
    [int]$TimeoutSec = 300,
    [string]$Out,
    [string]$ImageOut
)
$ErrorActionPreference = 'Stop'

function Resolve-Target {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $Path))
}

$argsObj = @{}
if ($ArgumentsFile -and (Test-Path -LiteralPath $ArgumentsFile)) {
    $raw = Get-Content -Raw -Encoding UTF8 -LiteralPath $ArgumentsFile
    if ($raw.Trim().Length -gt 0) { $argsObj = $raw | ConvertFrom-Json }
}

# 有副作用的工具需要项目上下文令牌（project_id:instance_id）。
if ($Tool -notmatch '^(get_|list_|inspect_|capture_|validate_|check_|search_|read_)') {
    try {
        $health = Invoke-WebRequest -Uri ($Uri -replace '/mcp$', '/health') -TimeoutSec 15 -UseBasicParsing
        $healthJson = $health.Content | ConvertFrom-Json
        $context = $healthJson.context.expected_context
        if ($context -and -not ($argsObj.PSObject.Properties.Name -contains 'expected_context')) {
            if ($argsObj -is [System.Collections.IDictionary]) {
                $argsObj['expected_context'] = $context
            } else {
                $argsObj | Add-Member -NotePropertyName 'expected_context' -NotePropertyValue $context -Force
            }
        }
    } catch {
        Write-Output ('MCP_CONTEXT_WARNING: ' + $_.Exception.Message)
    }
}
$body = @{
    jsonrpc = '2.0'
    id      = 2
    method  = 'tools/call'
    params  = @{ name = $Tool; arguments = $argsObj }
}
$json = $body | ConvertTo-Json -Depth 40 -Compress
$bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
$response = Invoke-WebRequest -Uri $Uri -Method Post -ContentType 'application/json' `
    -Headers @{ Accept = 'application/json, text/event-stream' } -Body $bytes -TimeoutSec $TimeoutSec -UseBasicParsing
$content = $response.Content
if ($content -match '(?m)^\s*data:\s*\{') {
    $content = (($content -split "`n") | Where-Object { $_ -match '^\s*data:\s*\{' } | ForEach-Object { $_ -replace '^\s*data:\s*', '' }) -join "`n"
}
$result = $content | ConvertFrom-Json
$textParts = @()
$imageSaved = $false
if ($result.result -and $result.result.content) {
    foreach ($part in $result.result.content) {
        if ($part.type -eq 'text') {
            $textParts += [string]$part.text
        } elseif ($part.type -eq 'image' -and $ImageOut -and -not $imageSaved) {
            $target = Resolve-Target $ImageOut
            $parent = Split-Path -Parent $target
            if ($parent -and -not (Test-Path -LiteralPath $parent)) {
                New-Item -ItemType Directory -Force -Path $parent | Out-Null
            }
            [System.IO.File]::WriteAllBytes($target, [System.Convert]::FromBase64String($part.data))
            $textParts += ('IMAGE_SAVED:' + $target)
            $imageSaved = $true
        }
    }
}
$textOut = ($textParts -join "`n")
if ($Out) {
    $target = Resolve-Target $Out
    $parent = Split-Path -Parent $target
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    [System.IO.File]::WriteAllText($target, $textOut, (New-Object System.Text.UTF8Encoding($false)))
    Write-Output ('WROTE ' + $target)
} else {
    Write-Output $textOut
}
if ($result.error) {
    Write-Output ('MCP_ERROR: ' + ($result.error | ConvertTo-Json -Compress))
    exit 3
}
