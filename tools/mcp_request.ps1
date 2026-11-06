# 向 Funplay MCP 服务器发送一条 JSON-RPC 请求（initialize / tools/list / ping），
# 并打印原始响应。
param(
    [Parameter(Mandatory = $true)][string]$Method,
    [string]$ParamsFile,
    [string]$Uri = 'http://127.0.0.1:8765/mcp',
    [int]$TimeoutSec = 120
)
$ErrorActionPreference = 'Stop'

$paramsObj = @{}
if ($ParamsFile -and (Test-Path -LiteralPath $ParamsFile)) {
    $raw = Get-Content -Raw -Encoding UTF8 -LiteralPath $ParamsFile
    if ($raw.Trim().Length -gt 0) { $paramsObj = $raw | ConvertFrom-Json }
}
$envelope = @{ jsonrpc = '2.0'; id = 1; method = $Method; params = $paramsObj }
$json = $envelope | ConvertTo-Json -Depth 30 -Compress
$bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
$response = Invoke-WebRequest -Uri $Uri -Method Post -ContentType 'application/json' `
    -Headers @{ Accept = 'application/json, text/event-stream' } -Body $bytes -TimeoutSec $TimeoutSec -UseBasicParsing
$content = $response.Content
if ($content -match '(?m)^\s*data:\s*\{') {
    $content = (($content -split "`n") | Where-Object { $_ -match '^\s*data:\s*\{' } | ForEach-Object { $_ -replace '^\s*data:\s*', '' }) -join "`n"
}
Write-Output $content
