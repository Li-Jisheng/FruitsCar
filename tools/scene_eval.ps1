# 在运行中的 Game View 预览里执行 JavaScript，只打印返回值（一行）。
# 预览未运行时以退出码 3 失败。
param(
    [Parameter(Mandatory = $true)][string]$Code,
    [string]$JsonArgs = '{}',
    [string]$Uri = 'http://127.0.0.1:8765/mcp',
    [int]$TimeoutSec = 60
)
$ErrorActionPreference = 'Stop'
$arguments = @{ context = 'scene'; code = $Code }
# 固定期望的编辑器会话（MCP 执行场景脚本时要求带上它）。
try {
    $health = Invoke-WebRequest -Uri ($Uri -replace '/mcp$', '/health') -TimeoutSec 15 -UseBasicParsing | ConvertFrom-Json
    if ($health.context.expected_context) { $arguments['expected_context'] = $health.context.expected_context }
} catch { }
if ($JsonArgs -and $JsonArgs -ne '{}') {
    $arguments['args'] = ($JsonArgs | ConvertFrom-Json)
}
$body = @{
    jsonrpc = '2.0'
    id      = 7
    method  = 'tools/call'
    params  = @{ name = 'execute_javascript'; arguments = $arguments }
}
$json = $body | ConvertTo-Json -Depth 20 -Compress
$bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
$response = Invoke-WebRequest -Uri $Uri -Method Post -ContentType 'application/json' `
    -Headers @{ Accept = 'application/json, text/event-stream' } -Body $bytes -TimeoutSec $TimeoutSec -UseBasicParsing
$content = $response.Content
if ($content -match '(?m)^\s*data:\s*\{') {
    $content = (($content -split "`n") | Where-Object { $_ -match '^\s*data:\s*\{' } |
        ForEach-Object { $_ -replace '^\s*data:\s*', '' }) -join "`n"
}
$result = $content | ConvertFrom-Json
if ($result.error) {
    Write-Output ('JS_ERROR ' + ($result.error | ConvertTo-Json -Compress))
    exit 3
}
if ($result.result -and $result.result.structuredContent) {
    $structured = $result.result.structuredContent
    if ($structured.data -and $structured.data.result -ne $null) {
        Write-Output ([string]$structured.data.result)
        exit 0
    }
    Write-Output ($structured | ConvertTo-Json -Depth 8 -Compress)
    exit 0
}
foreach ($part in $result.result.content) {
    if ($part.type -eq 'text') { Write-Output ([string]$part.text) }
}
exit 0
