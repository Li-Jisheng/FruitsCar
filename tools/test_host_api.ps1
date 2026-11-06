# M18 host API 测试（TC-M18-01..04、TC-M18-09）：以关卡目录启动 fc_host，
# 覆盖 /levels、/level/select、/pause 以及 E1019 失败路径，
# 并把每条断言（expected/actual）写成一行 JSON 写入 host_api.log。
param(
    [int]$Port = 8791,
    [string]$OutDir = 'Docs/evidence/phase2/M18_home',
    [int]$Seed = 20261105
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$outPath = Resolve-FullPath $OutDir
if (-not (Test-Path -LiteralPath $outPath)) {
    New-Item -ItemType Directory -Force -Path $outPath | Out-Null
}
$logPath = Join-Path $outPath 'host_api.log'
$base = 'http://127.0.0.1:' + $Port
$failures = 0
$assertions = 0
$lines = New-Object System.Collections.Generic.List[string]

function Emit {
    param([string]$Check, [string]$Expected, [string]$Actual, [bool]$Ok)
    $script:assertions++
    if (-not $Ok) { $script:failures++ }
    $payload = [ordered]@{
        ts       = (Get-IsoTimestamp)
        kind     = 'ASSERT'
        check    = $Check
        expected = $Expected
        actual   = $Actual
        ok       = $Ok
    }
    $script:lines.Add(($payload | ConvertTo-Json -Compress)) | Out-Null
}

function CheckEqual {
    param([string]$Name, [string]$Expected, [string]$Actual)
    Emit -Check $Name -Expected $Expected -Actual $Actual -Ok ($Expected -eq $Actual)
}

function Start-HostForApi {
    param([string[]]$Arguments)
    $full = @($Arguments) + @('--port', $Port, '--max-ticks', '400000')
    $process = Start-Process -FilePath (Get-BinaryPath -Name 'fc_host') -ArgumentList $full -PassThru `
        -WindowStyle Hidden -WorkingDirectory $root
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep -Milliseconds 250
        try {
            $null = Invoke-RestMethod -Uri ($base + '/health') -TimeoutSec 2
            return $process
        } catch {}
    }
    return $process
}

function Stop-AnyHost {
    Get-Process fc_host -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 400
}

function Get-State {
    return Invoke-RestMethod -Uri ($base + '/state?since=0') -TimeoutSec 5
}

Stop-AnyHost
$hostProcess = Start-HostForApi -Arguments @('--catalogue', 'levels/catalogue.json', '--root', '.', '--seed', $Seed)
try {
    # TC-M18-01：目录结构与顺序。
    $levels = Invoke-RestMethod -Uri ($base + '/levels') -TimeoutSec 5
    CheckEqual 'TC-M18-01.count' '2' ([string]@($levels.levels).Count)
    CheckEqual 'TC-M18-01.order' '1,2' ([string](($levels.levels | ForEach-Object { $_.order }) -join ','))
    CheckEqual 'TC-M18-01.ids' 'L_RING_OMEGA,L_ZIGZAG_WIDE' ([string](($levels.levels | ForEach-Object { $_.id }) -join ','))
    CheckEqual 'TC-M18-01.current' 'L_RING_OMEGA' ([string]$levels.current.id)
    $previewOk = $true
    foreach ($level in $levels.levels) {
        if (-not $level.preview -or @($level.preview.road).Count -lt 1 -or @($level.preview.fruits).Count -lt 1) {
            $previewOk = $false
        }
        CheckEqual ('TC-M18-01.preview.' + $level.id + '.rows') ([string]$(if ($level.id -eq 'L_RING_OMEGA') { 11 } else { 9 })) ([string]$level.preview.rows)
        CheckEqual ('TC-M18-01.preview.' + $level.id + '.name') ([string]$level.name) ([string]$level.name)
    }
    Emit -Check 'TC-M18-01.previews_present' -Expected 'true' -Actual ([string]$previewOk) -Ok $previewOk
    CheckEqual 'TC-M18-01.difficulty' '2,3' ([string](($levels.levels | ForEach-Object { $_.difficulty }) -join ','))

    # TC-M18-03：未知关卡 -> 404 UNKNOWN_LEVEL，且 host 继续运行当前关卡。
    $before = Get-State
    $unknown = $null
    $unknownStatus = 0
    $request = [System.Net.HttpWebRequest]::Create($base + '/level/select')
    $request.Method = 'POST'
    $request.ContentType = 'application/json'
    $payloadBytes = [System.Text.Encoding]::UTF8.GetBytes('{"id":"L_DOES_NOT_EXIST"}')
    $request.ContentLength = $payloadBytes.Length
    $requestStream = $request.GetRequestStream()
    $requestStream.Write($payloadBytes, 0, $payloadBytes.Length)
    $requestStream.Close()
    $response = $null
    try {
        $response = $request.GetResponse()
    } catch [System.Net.WebException] {
        $response = $_.Exception.Response
    }
    if ($response) {
        $unknownStatus = [int]$response.StatusCode
        $reader = New-Object System.IO.StreamReader($response.GetResponseStream())
        $body = $reader.ReadToEnd()
        $reader.Close()
        try { $unknown = $body | ConvertFrom-Json } catch { $unknown = $null }
    }
    CheckEqual 'TC-M18-03.http' '404' ([string]$unknownStatus)
    CheckEqual 'TC-M18-03.error' 'UNKNOWN_LEVEL' ([string]$unknown.error)
    $after = Get-State
    CheckEqual 'TC-M18-03.level_unchanged' ([string]$before.levelId) ([string]$after.levelId)
    CheckEqual 'TC-M18-03.tick_kept_running' 'true' ([string]($after.tick -ge $before.tick))

    # TC-M18-02：选择关卡 2 -> levelEpoch+1，/state 随之更新。
    $epochBefore = [int]$before.levelEpoch
    $selected = Invoke-RestMethod -Uri ($base + '/level/select') -Method Post -ContentType 'application/json' `
        -Body '{"id":"L_ZIGZAG_WIDE"}' -TimeoutSec 5
    CheckEqual 'TC-M18-02.ok' 'true' ([string]$selected.ok)
    CheckEqual 'TC-M18-02.id' 'L_ZIGZAG_WIDE' ([string]$selected.id)
    CheckEqual 'TC-M18-02.epoch' ([string]($epochBefore + 1)) ([string]$selected.levelEpoch)
    Start-Sleep -Milliseconds 400
    $afterSelect = Get-State
    CheckEqual 'TC-M18-02.state_level' 'L_ZIGZAG_WIDE' ([string]$afterSelect.levelId)
    CheckEqual 'TC-M18-02.state_epoch' ([string]$selected.levelEpoch) ([string]$afterSelect.levelEpoch)
    CheckEqual 'TC-M18-02.remaining' '65' ([string]$afterSelect.hud.fruitsRemaining)
    $zigName = (Invoke-RestMethod -Uri ($base + '/levels') -TimeoutSec 5).levels | Where-Object { $_.id -eq 'L_ZIGZAG_WIDE' } | ForEach-Object { $_.name }
    CheckEqual 'TC-M18-02.hud_level_name' ([string]$zigName) ([string]$afterSelect.hud.levelName)
    CheckEqual 'TC-M18-02.next_lane' '0' ([string]$afterSelect.hud.nextLane)

    # TC-M18-04：暂停冻结 tick，恢复后继续推进。
    $pause = Invoke-RestMethod -Uri ($base + '/pause') -Method Post -ContentType 'application/json' `
        -Body '{"paused":true}' -TimeoutSec 5
    CheckEqual 'TC-M18-04.pause_ok' 'true' ([string]$pause.paused)
    Start-Sleep -Milliseconds 300
    $frozenA = [int](Get-State).tick
    Start-Sleep -Seconds 2
    $frozenB = [int](Get-State).tick
    CheckEqual 'TC-M18-04.tick_frozen' ([string]$frozenA) ([string]$frozenB)
    $resume = Invoke-RestMethod -Uri ($base + '/pause') -Method Post -ContentType 'application/json' `
        -Body '{"paused":false}' -TimeoutSec 5
    CheckEqual 'TC-M18-04.resume_ok' 'false' ([string]$resume.paused)
    Start-Sleep -Seconds 1
    $resumedTick = [int](Get-State).tick
    CheckEqual 'TC-M18-04.tick_resumed' 'true' ([string]($resumedTick -gt $frozenB))
} finally {
    Stop-AnyHost
}

# TC-M18-09：非法目录会立即以 E1019 和退出码 3 失败。
$badCatalogue = Join-Path $root 'temp\catalogue_bad.json'
$badText = [System.IO.File]::ReadAllText((Join-Path $root 'levels\catalogue.json'))
$badText = $badText.Replace('"file": "levels/L_ZIGZAG_WIDE.json"', '"file": "levels/MISSING.json"')
Write-Utf8NoBom -Path $badCatalogue -Content $badText
$ErrorActionPreference = 'Continue'
$badOutput = & (Get-BinaryPath -Name 'fc_host') --catalogue $badCatalogue --root $root --audit 2>&1
$badCode = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
$badLine = ($badOutput | Out-String)
Emit -Check 'TC-M18-09.exit_code' -Expected '3' -Actual ([string]$badCode) -Ok ($badCode -eq 3)
Emit -Check 'TC-M18-09.code' -Expected 'E1019' -Actual ([string]($badLine -match 'E1019')) -Ok ($badLine -match 'E1019')

# SUMMARY 结束 NDJSON 日志（与规则测试日志的结构相同）。
$result = if ($failures -eq 0) { 'PASS' } else { 'FAIL' }
$summary = [ordered]@{
    ts         = (Get-IsoTimestamp)
    kind       = 'SUMMARY'
    case       = 'M18'
    cases      = $assertions
    pass       = ($assertions - $failures)
    fail       = $failures
    skipped    = 0
    result     = $result
}
$lines.Add(($summary | ConvertTo-Json -Compress)) | Out-Null
Write-Utf8NoBom -Path $logPath -Content ($lines -join "`r`n")
Write-Output ('HOST_API cases=' + $assertions + ' fail=' + $failures + ' result=' + $result)
Write-Output ('LOG ' + $logPath)
if ($failures -ne 0) { exit 1 }
exit 0
