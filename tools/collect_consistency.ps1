param([string]$Out = 'Docs/evidence/phase1/M10_client_view')
$ErrorActionPreference = 'Stop'
if ([System.IO.Path]::IsPathRooted($Out)) { $outDir = [System.IO.Path]::GetFullPath($Out) } else { $outDir = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $Out)) }
Write-Output ("outDir=" + $outDir)
$hostLogs = @(Get-ChildItem -LiteralPath $outDir -Filter 'host_*.ndjson' -ErrorAction SilentlyContinue)
Write-Output ("hostLogs=" + $hostLogs.Count)
$json = @()
foreach ($logFile in $hostLogs) {
    Write-Output ("scan " + $logFile.Name)
    foreach ($line in (Get-Content -LiteralPath $logFile.FullName)) {
        if ($line -match '"kind":"CLIENT"') {
            try {
                $obj = $line | ConvertFrom-Json
                if ($obj.payload) { $json += ($obj.payload | ConvertTo-Json -Compress -Depth 6) }
            } catch {
                Write-Output ("parse error: " + $_.Exception.Message)
            }
        }
    }
}
Write-Output ("collected=" + $json.Count)
if ($json.Count -gt 0) {
    $target = Join-Path $outDir 'consistency_nodes.log'
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($target, (($json -join "`r`n") + "`r`n"), $encoding)
    Write-Output ("wrote " + $target)
}
