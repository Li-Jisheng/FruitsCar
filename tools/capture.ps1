# 从正在运行的 Cocos Creator 预览中抓取 M10 视觉证据。
# 预览必须处于运行状态（Game View），且客户端轮询 127.0.0.1:8787。
param(
    [string]$Preset = 'M10_flow',
    [string]$Out = 'Docs/evidence/phase1/M10_client_view',
    [string]$HostUrl = 'http://127.0.0.1:8787',
    [int]$Seed = 20261104
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$outDir = Resolve-FullPath $Out
if (-not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}
$rawPath = Join-Path $root 'temp\capture_raw.png'
$cropScript = Join-Path $PSScriptRoot 'crop_shot.py'
$hostExe = Get-BinaryPath -Name 'fc_host'
$hostProcess = $null

function Save-Shot {
    param([string]$Name)
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'mcp_call.ps1') `
        -Tool capture_editor_screenshot -ImageOut $rawPath *> $null
    if (-not (Test-Path -LiteralPath $rawPath)) {
        Write-Output ("SHOT_FAILED " + $Name)
        return $false
    }
    $target = Join-Path $outDir $Name
    $python = $env:FC_PYTHON
    if (-not $python) {
        $python = 'D:\ProgrammingSoftware\DeepseekHarness\dsh-home\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'
    }
    if (-not (Test-Path -LiteralPath $python)) {
        $found = Get-Command python -ErrorAction SilentlyContinue
        if ($found) { $python = $found.Source }
    }
    if ($python -and (Test-Path -LiteralPath $python)) {
        & $python $cropScript $rawPath $target 2>$null | Out-Null
    }
    if (-not (Test-Path -LiteralPath $target)) {
        Copy-Item -LiteralPath $rawPath -Destination $target -Force
    }
    $size = (Get-Item -LiteralPath $target).Length
    Write-Output ("SHOT " + $Name + " " + $size + " bytes")
    return ($size -gt 0)
}

function Post-Command {
    param([string]$Body)
    try {
        return Invoke-RestMethod -Uri ($HostUrl + '/command') -Method Post -ContentType 'application/json' `
            -Body $Body -TimeoutSec 5
    } catch {
        return $null
    }
}

function Get-State {
    try {
        return Invoke-RestMethod -Uri ($HostUrl + '/state?since=0') -Method Get -TimeoutSec 5
    } catch {
        return $null
    }
}

function Start-Host {
    param([string]$Level, [string]$Mode, [string]$LogName)
    $logPath = Join-Path $outDir $LogName
    $arguments = @('--level', $Level, '--seed', $Seed, '--port', '8787', '--log', $logPath, '--max-ticks', '40000')
    if ($Mode) {
        $arguments += @('--autoplay', $Mode)
    }
    $script:hostProcess = Start-Process -FilePath $hostExe -ArgumentList $arguments -PassThru -WindowStyle Hidden
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 250
        if (Get-State) { break }
    }
}

function Stop-Host {
    if ($script:hostProcess -and -not $script:hostProcess.HasExited) {
        Stop-Process -Id $script:hostProcess.Id -Force -ErrorAction SilentlyContinue
    }
    $script:hostProcess = $null
    # 绝不能在端口上留下残留的 host：否则客户端会一直与它通信。
    foreach ($stale in (Get-Process fc_host -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $stale.Id -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 500
}

function Wait-For {
    param([scriptblock]$Condition, [int]$TimeoutSeconds = 90)
    for ($i = 0; $i -lt ($TimeoutSeconds * 4); $i++) {
        $state = Get-State
        if ($state -and (& $Condition $state)) {
            return $true
        }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

function Collect-Consistency {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'collect_consistency.ps1') -Out $outDir
}

function Collect-Diagnostics {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'mcp_call.ps1') `
        -Tool run_script_diagnostics -Out (Join-Path $outDir 'diagnostics.log') *> $null
    Write-Output 'DIAGNOSTICS saved'
}

switch ($Preset) {
    'M10_flow' {
        Stop-Host
        Start-Host -Level 'tests/levels/L01_default.json' -Mode '' -LogName 'host_m10.ndjson'
        Start-Sleep -Seconds 1
        Save-Shot 'shot_01_initial.png' | Out-Null

        # 选出队首车辆与路旁水果同色数量最多的车道，
        # 这样收集截图里确实能看到水果消失。
        $state = Get-State
        $bestLane = 0
        $bestScore = -1
        if ($state) {
            $rows = [int]$state.grid.rows
            $cols = [int]$state.grid.cols
            $roadside = @{}
            for ($c = 0; $c -lt $cols; $c++) {
                $cell = $state.grid.cells[$rows - 1][$c]
                if ($cell -and $cell.settle -eq 0) {
                    $roadside[$cell.fruit] = 1 + ($(if ($roadside.ContainsKey($cell.fruit)) { $roadside[$cell.fruit] } else { 0 }))
                }
            }
            for ($i = 0; $i -lt $state.lanes.Count; $i++) {
                $queue = @($state.lanes[$i].queue)
                if ($queue.Count -eq 0) { continue }
                $info = $state.vehicleInfo.($queue[0])
                if (-not $info) { continue }
                $score = 0
                if ($roadside.ContainsKey($info.color)) { $score = $roadside[$info.color] }
                if ($score -gt $bestScore) { $bestScore = $score; $bestLane = $i }
            }
        }
        $receipt = Post-Command ('{"type":"select_lane","lane":' + $bestLane + '}')
        Write-Output ('COMMAND select_lane lane=' + $bestLane + ' -> ' + ($receipt | ConvertTo-Json -Compress))
        Start-Sleep -Milliseconds 450
        Save-Shot 'shot_02_after_select.png' | Out-Null

        $collectedBefore = [int](Get-State).hud.fruitsCollected
        for ($i = 0; $i -lt 40; $i++) {
            Start-Sleep -Milliseconds 200
            $current = Get-State
            if ($current -and [int]$current.hud.fruitsCollected -gt $collectedBefore) { break }
        }
        Save-Shot 'shot_03_collecting.png' | Out-Null

        for ($i = 0; $i -lt 5; $i++) {
            Post-Command ('{"type":"select_lane","lane":' + (($bestLane + 1) % 3) + '}') | Out-Null
            Start-Sleep -Milliseconds 40
        }
        Save-Shot 'shot_04_cooldown.png' | Out-Null
        Stop-Host
        Collect-Consistency
        Collect-Diagnostics
    }
    'M10_win' {
        Stop-Host
        Start-Host -Level 'tests/levels/L01_default.json' -Mode 'greedy' -LogName 'host_win.ndjson'
        $ok = Wait-For -Condition { param($s) $s.hud.result -ne $null } -TimeoutSeconds 120
        Start-Sleep -Milliseconds 600
        Save-Shot 'shot_06_win.png' | Out-Null
        Stop-Host
        Collect-Consistency
        Write-Output ('WIN_REACHED ' + $ok)
    }
    'M10_require_select' {
        Stop-Host
        Start-Host -Level 'tests/levels/L_route_park.json' -Mode '' -LogName 'host_require_select.ndjson'
        # 两次普通的选车道就会占满唯一的停车位，让第二辆车
        # 停在公路末端等待：这就是 require-select 状态。
        for ($car = 0; $car -lt 2; $car++) {
            for ($attempt = 0; $attempt -lt 40; $attempt++) {
                $receipt = Post-Command '{"type":"select_lane","lane":0}'
                if ($receipt -and $receipt.accepted) {
                    Write-Output ('COMMAND car=' + $car + ' accepted tick-next=' + $receipt.nextAllowedTick)
                    break
                }
                Start-Sleep -Milliseconds 200
            }
            Start-Sleep -Milliseconds 1200
        }
        $ok = Wait-For -Condition { param($s) $s.hud.requireSelect -eq $true } -TimeoutSeconds 60
        Start-Sleep -Milliseconds 400
        Save-Shot 'shot_05_require_select.png' | Out-Null
        Stop-Host
        Collect-Consistency
        Write-Output ('REQUIRE_SELECT_REACHED ' + $ok)
    }
    'M10_fail' {
        Stop-Host
        Start-Host -Level 'tests/levels/L_jam.json' -Mode 'spam' -LogName 'host_fail.ndjson'
        Start-Sleep -Seconds 2
        $ok = Wait-For -Condition { param($s) $s.hud.result -ne $null } -TimeoutSeconds 60
        Start-Sleep -Milliseconds 600
        Save-Shot 'shot_07_fail.png' | Out-Null
        Stop-Host
        Collect-Consistency
        Write-Output ('FAIL_REACHED ' + $ok)
    }
    default {
        Write-Output ('unknown preset ' + $Preset)
        exit 2
    }
}
exit 0
