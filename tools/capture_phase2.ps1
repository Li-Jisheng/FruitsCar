# Phase-2 视觉证据：驱动正在运行的 Game View 预览，抓取
# 首页、两个关卡和结算面板。
#
#   1. 游戏视图预览必须处于运行状态（Cocos Creator: Play）
#   2. 本地 host 必须能在 127.0.0.1:8787 上启动（由脚本自己占用）
#
# 游戏渲染在编辑器的 "Preview" 面板内；`-Crop` 就是该面板在
# 截图窗口中的精确矩形（设备像素）。脚本会按 720x1280 的画布宽高比
# 校验它，这样布局不对时会直接失败，而不是产生
# 误导性的截图。
param(
    [string]$Preset = 'home',
    [string]$OutDir = 'Docs/evidence/phase2',
    [int]$CropX = 923,
    [int]$CropY = 165,
    [int]$CropW = 716,
    [int]$CropH = 1262,
    [int]$Seed = 20261105,
    [double]$DpiScale = 1.5
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$outRoot = Resolve-FullPath $OutDir
$raw = Join-Path $root 'temp\capture_raw.png'
$python = $env:FC_PYTHON
if (-not $python) {
    $python = 'D:\ProgrammingSoftware\DeepseekHarness\dsh-home\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'
}
$aspect = $CropW / $CropH
if ([Math]::Abs($aspect - (720.0 / 1280.0)) -gt 0.02) {
    Write-Output ("CROP_ASPECT_INVALID " + $aspect)
    exit 2
}

function Get-HostExe {
    $candidate = Get-BinaryPath -Name 'fc_host'
    if (-not $candidate) { throw 'fc_host.exe not found - run tools/build.ps1 first' }
    return $candidate
}

function Stop-Host {
    if ($script:hostProcess -and -not $script:hostProcess.HasExited) {
        Stop-Process -Id $script:hostProcess.Id -Force -ErrorAction SilentlyContinue
    }
    $script:hostProcess = $null
    foreach ($stale in (Get-Process fc_host -ErrorAction SilentlyContinue)) {
        Stop-Process -Id $stale.Id -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 600
}

function Start-Host {
    param([string]$LevelArg, [string[]]$Extra, [string]$LogName)
    $logPath = Join-Path $outRoot $LogName
    $logDir = Split-Path -Parent $logPath
    if (-not (Test-Path -LiteralPath $logDir)) {
        New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    }
    $arguments = @()
    if ($LevelArg.StartsWith('catalogue:')) {
        $arguments += @('--catalogue', $LevelArg.Substring(10), '--root', '.')
    } else {
        $arguments += @('--level', $LevelArg, '--seed', $Seed)
    }
    $arguments += @('--port', '8787', '--max-ticks', '400000', '--log', $logPath)
    $arguments += $Extra
    $script:hostProcess = Start-Process -FilePath (Get-HostExe) -ArgumentList $arguments -PassThru `
        -WindowStyle Hidden -WorkingDirectory $root
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep -Milliseconds 250
        if ($script:hostProcess.HasExited) {
            Write-Output ('HOST_EXITED code=' + $script:hostProcess.ExitCode + ' log=' + $logPath)
            return $false
        }
        try {
            $null = Invoke-RestMethod -Uri 'http://127.0.0.1:8787/health' -TimeoutSec 2
            return $true
        } catch {}
    }
    return $false
}

function Get-State {
    try {
        return Invoke-RestMethod -Uri 'http://127.0.0.1:8787/state?since=0' -Method Get -TimeoutSec 5
    } catch {
        return $null
    }
}

function Get-Levels {
    try {
        return Invoke-RestMethod -Uri 'http://127.0.0.1:8787/levels' -Method Get -TimeoutSec 5
    } catch {
        return $null
    }
}

function Select-Level {
    param([string]$Id)
    try {
        return Invoke-RestMethod -Uri 'http://127.0.0.1:8787/level/select' -Method Post `
            -ContentType 'application/json' -Body ('{"id":"' + $Id + '"}') -TimeoutSec 5
    } catch {
        return $null
    }
}

function Set-Paused {
    param([bool]$Paused)
    try {
        return Invoke-RestMethod -Uri 'http://127.0.0.1:8787/pause' -Method Post `
            -ContentType 'application/json' -Body ('{"paused":' + $Paused.ToString().ToLower() + '}') -TimeoutSec 5
    } catch {
        return $null
    }
}

function Post-Command {
    param([string]$Body)
    try {
        return Invoke-RestMethod -Uri 'http://127.0.0.1:8787/command' -Method Post `
            -ContentType 'application/json' -Body $Body -TimeoutSec 5
    } catch {
        return $null
    }
}

function Wait-For {
    param([scriptblock]$Condition, [int]$TimeoutSeconds = 60)
    for ($i = 0; $i -lt ($TimeoutSeconds * 4); $i++) {
        $state = Get-State
        if ($state -and (& $Condition $state)) { return $true }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

# 设计空间（720x1280）坐标 -> 游戏面板内的屏幕坐标点击。
# 截图位图使用设备像素，而系统光标工作在
# （虚拟化的）100% 空间，因此两个轴都要除以显示缩放比例。
function Click-Game {
    param([double]$DesignX, [double]$DesignY)
    $x = [int][Math]::Round(($CropX + $DesignX * $CropW / 720.0) / $DpiScale)
    $y = [int][Math]::Round(($CropY + $DesignY * $CropH / 1280.0) / $DpiScale)
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'click_at.ps1') `
        -X $x -Y $y *> $null
    Start-Sleep -Milliseconds 250
}

function Save-Shot {
    param([string]$Name, [string]$SubDir)
    $directory = Join-Path $outRoot $SubDir
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'mcp_call.ps1') `
        -Tool capture_editor_screenshot -ImageOut $raw *> $null
    if (-not (Test-Path -LiteralPath $raw)) {
        Write-Output ("SHOT_FAILED " + $Name)
        return $false
    }
    $target = Join-Path $directory $Name
    & $python (Join-Path $PSScriptRoot 'crop_rect.py') $raw $target $CropX $CropY $CropW $CropH | Out-Null
    if (-not (Test-Path -LiteralPath $target)) {
        Write-Output ("SHOT_FAILED " + $Name)
        return $false
    }
    $size = (Get-Item -LiteralPath $target).Length
    Write-Output ("SHOT " + $SubDir + "/" + $Name + " " + $size + " bytes")
    return ($size -gt 0)
}

function Write-Json {
    param([string]$Path, [object]$Object)
    Write-Utf8NoBom -Path $Path -Content ($Object | ConvertTo-Json -Depth 8)
}

switch ($Preset) {
    'home' {
        Stop-Host
        $ok = Start-Host -LevelArg 'catalogue:levels/catalogue.json' -Extra @() -LogName 'M18_home/host_home.ndjson'
        # 全新的客户端在 host 运行时启动，会停在首页。
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'restart_preview.ps1') | Out-Null
        Write-Output ('HOST_STARTED ' + $ok)
        Start-Sleep -Seconds 2
        $levels = Get-Levels
        if ($levels) {
            Write-Json -Path (Join-Path $outRoot 'M18_home/levels.json') -Object $levels
            Write-Output ('LEVELS cards=' + @($levels.levels).Count + ' current=' + $levels.current.id)
        }
        Save-Shot 'shot_01_home.png' 'M18_home'
        # S3：点击第 1 张卡片，并立即抓取加载状态。
        Click-Game -DesignX 360 -DesignY 418
        Save-Shot 'shot_02_loading.png' 'M18_home'
        Start-Sleep -Seconds 2
        $state = Get-State
        Write-Output ('GAME_LEVEL ' + $state.levelId + ' epoch=' + $state.levelEpoch + ' tick=' + $state.tick)
        Save-Shot 'shot_03_game_l1.png' 'M18_home'

        # 返回首页：该按钮必须暂停 host（TC-M18-08）。
        Click-Game -DesignX 48 -DesignY 44
        Start-Sleep -Milliseconds 1500
        $beforeState = Get-State
        $beforeTick = [int]$beforeState.tick
        Start-Sleep -Seconds 2
        $afterState = Get-State
        $afterTick = [int]$afterState.tick
        $frozen = ($afterTick -eq $beforeTick)
        Save-Shot 'shot_04_back_home.png' 'M18_home'
        $lines = @()
        $lines += ('before_tick=' + $beforeTick)
        $lines += ('after_tick=' + $afterTick)
        $lines += ('paused=' + $afterState.paused)
        $lines += ('tick_frozen=' + $frozen)
        Write-Utf8NoBom -Path (Join-Path $outRoot 'M18_home/shot_03_back_paused.txt') -Content ($lines -join "`r`n")
        Write-Output ('PAUSE_FROZEN ' + $frozen + ' paused=' + $afterState.paused)

        # TC-M18-04：显式调用 /pause 必须冻结 tick，/pause false 恢复运行。
        $null = Set-Paused $true
        $frozenTick = [int](Get-State).tick
        Start-Sleep -Seconds 2
        $stillFrozen = ([int](Get-State).tick -eq $frozenTick)
        $null = Set-Paused $false
        Start-Sleep -Seconds 1
        $resumed = ([int](Get-State).tick -gt $frozenTick)
        Write-Output ('PAUSE_API frozen=' + $stillFrozen + ' resumed=' + $resumed)
        $lines += ('api_frozen_tick=' + $frozenTick)
        $lines += ('api_frozen=' + $stillFrozen)
        $lines += ('api_resumed=' + $resumed)
        Write-Utf8NoBom -Path (Join-Path $outRoot 'M18_home/shot_03_back_paused.txt') -Content ($lines -join "`r`n")
        Stop-Host
    }
    'level1' {
        Stop-Host
        $ok = Start-Host -LevelArg 'levels/L_RING_OMEGA.json' -Extra @() -LogName 'M16_level1/host_initial.ndjson'
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'restart_preview.ps1') | Out-Null
        Write-Output ('HOST_STARTED ' + $ok)
        Start-Sleep -Seconds 2
        Click-Game -DesignX 360 -DesignY 418
        Start-Sleep -Seconds 2
        $state = Get-State
        Write-Output ('GAME_LEVEL ' + $state.levelId + ' remaining=' + $state.hud.fruitsRemaining)
        Save-Shot 'shot_01_initial.png' 'M16_level1'

        # 派出蓝色车辆（2 号停车道），让内圈被收集，
        # 外圈则明显滑入并收缩。
        $receipt = Post-Command '{"type":"select_lane","lane":2}'
        Write-Output ('COMMAND select_lane lane=2 -> ' + ($receipt | ConvertTo-Json -Compress))
        $null = Wait-For -Condition { param($s) [int]$s.hud.fruitsCollected -ge 3 } -TimeoutSeconds 40
        Save-Shot 'shot_02_collecting.png' 'M16_level1'
        $null = Wait-For -Condition { param($s) [int]$s.hud.fruitsCollected -ge 9 } -TimeoutSeconds 40
        Start-Sleep -Milliseconds 400
        Save-Shot 'shot_03_refilled.png' 'M16_level1'
        Stop-Host
    }
    'level2' {
        Stop-Host
        $ok = Start-Host -LevelArg 'levels/L_ZIGZAG_WIDE.json' -Extra @() -LogName 'M17_level2/host_initial.ndjson'
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'restart_preview.ps1') | Out-Null
        Write-Output ('HOST_STARTED ' + $ok)
        Start-Sleep -Seconds 2
        Click-Game -DesignX 360 -DesignY 418
        Start-Sleep -Seconds 2
        $state = Get-State
        Write-Output ('GAME_LEVEL ' + $state.levelId + ' remaining=' + $state.hud.fruitsRemaining)
        Save-Shot 'shot_01_initial.png' 'M17_level2'
        $r1 = Post-Command '{"type":"select_lane","lane":2}'
        Write-Output ('COMMAND lane=2 (blue, lane 0) -> ' + ($r1 | ConvertTo-Json -Compress))
        Start-Sleep -Milliseconds 700
        Save-Shot 'shot_03_next_lane.png' 'M17_level2'
        $null = Wait-For -Condition { param($s) [int]$s.hud.fruitsCollected -ge 2 } -TimeoutSeconds 30
        # 第二辆车走 1 号车道，这样两辆车就并排行驶。
        for ($i = 0; $i -lt 40; $i++) {
            $r2 = Post-Command '{"type":"select_lane","lane":0}'
            if ($r2 -and $r2.accepted) { break }
            Start-Sleep -Milliseconds 250
        }
        $null = Wait-For -Condition { param($s) @($s.vehicles).Count -ge 2 } -TimeoutSeconds 30
        Start-Sleep -Milliseconds 300
        Save-Shot 'shot_02_two_lanes.png' 'M17_level2'
        Stop-Host
    }
    'win' {
        Stop-Host
        $ok = Start-Host -LevelArg 'levels/L_RING_OMEGA.json' -Extra @('--autoplay','greedy') -LogName 'M19_e2e/home_to_l1_win.ndjson'
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'restart_preview.ps1') | Out-Null
        Write-Output ('HOST_STARTED ' + $ok)
        Start-Sleep -Seconds 2
        Click-Game -DesignX 360 -DesignY 418
        $reached = Wait-For -Condition { param($s) $s.hud.result -ne $null } -TimeoutSeconds 180
        Start-Sleep -Milliseconds 800
        Save-Shot 'shot_04_result_win.png' 'M19_e2e'
        Write-Output ('WIN_REACHED ' + $reached)
        Stop-Host
    }
    'jam' {
        Stop-Host
        $ok = Start-Host -LevelArg 'tests/levels/L_p2_jam.json' -Extra @('--autoplay','spam') -LogName 'M19_e2e/jam.ndjson'
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'restart_preview.ps1') | Out-Null
        Write-Output ('HOST_STARTED ' + $ok)
        Start-Sleep -Seconds 2
        Click-Game -DesignX 360 -DesignY 418
        $reached = Wait-For -Condition { param($s) $s.hud.result -ne $null } -TimeoutSeconds 90
        Start-Sleep -Milliseconds 800
        Save-Shot 'shot_05_result_jam.png' 'M19_e2e'
        Write-Output ('JAM_REACHED ' + $reached)
        Stop-Host
    }
    default {
        Write-Output ('unknown preset ' + $Preset)
        exit 2
    }
}
exit 0
