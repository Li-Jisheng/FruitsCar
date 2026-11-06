# 刷新脚本资源并重启 Game View 预览，
# 让刚编辑过的 TypeScript 被预览包重新加载。
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/refresh_preview.ps1
param(
    [string]$Uri = 'http://127.0.0.1:8765/mcp',
    [int]$WaitSeconds = 8
)
$ErrorActionPreference = 'Stop'
$code = 'return (async () => { await Editor.Message.request("asset-db", "refresh-asset", "db://assets/scripts"); return "refreshed"; })();'
$arguments = @{ context = 'editor'; code = $code }
try {
    $health = Invoke-WebRequest -Uri ($Uri -replace '/mcp$', '/health') -TimeoutSec 15 -UseBasicParsing | ConvertFrom-Json
    if ($health.context.expected_context) {
        $arguments['expected_context'] = $health.context.expected_context
    }
} catch {}
$body = @{ jsonrpc = '2.0'; id = 21; method = 'tools/call'
    params = @{ name = 'execute_javascript'; arguments = $arguments } }
$json = $body | ConvertTo-Json -Depth 20 -Compress
$bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
$response = Invoke-WebRequest -Uri $Uri -Method Post -ContentType 'application/json' `
    -Headers @{ Accept = 'application/json, text/event-stream' } -Body $bytes -TimeoutSec 120 -UseBasicParsing
$content = $response.Content
if ($content -match '(?m)^\s*data:\s*\{') {
    $content = (($content -split "`n") | Where-Object { $_ -match '^\s*data:\s*\{' } |
        ForEach-Object { $_ -replace '^\s*data:\s*', '' }) -join "`n"
}
$result = $content | ConvertFrom-Json
if ($result.error) {
    Write-Output ('REFRESH_FAILED ' + ($result.error | ConvertTo-Json -Compress))
    exit 2
}
Write-Output 'REFRESHED'
Start-Sleep -Seconds $WaitSeconds
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'restart_preview.ps1')
exit $LASTEXITCODE
