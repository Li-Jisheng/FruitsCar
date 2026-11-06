# Phase-3 视觉证据（E3）：驱动正在运行的 Game View 预览，抓取
# 弯道公路、弯道水果层、动画帧以及结算画面。
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/capture_phase3.ps1 -Preset curve
#
# 前置条件：fc_host 已构建，Cocos Creator 已打开 assets/scenes/Main.scene，
# 且 Game View 预览可用。脚本自己占用 :8787 上的 host 进程。
param(
    [string]$Preset = 'curve',
    [string]$Level = 'levels/L_CURVE_RING.json',
    [string]$LevelId = 'L_CURVE_RING',
    [string]$OutDir = 'Docs/evidence/framework/M24_client_curve',
    [int]$Seed = 20261107,
    [string]$Autoplay = 'greedy',
    [int]$Frames = 8,
    [int]$FrameGapMs = 260,
    [int]$Port = 8787,
    [double]$DpiScale = 1.5
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$outRoot = Resolve-FullPath $OutDir
if (-not (Test-Path -LiteralPath $outRoot)) { New-Item -ItemType Directory -Force -Path $outRoot | Out-Null }
$python = $env:FC_PYTHON
if (-not $python) {
    $python = 'D:\ProgrammingSoftware\DeepseekHarness\dsh-home\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'
}
$base = "http://127.0.0.1:$Port"
$shotIndex = 0
$shotLog = New-Object System.Collections.Generic.List[object]
$panel = $null

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
    $exe = Get-BinaryPath -Name 'fc_host'
    if (-not $exe) { throw 'fc_host.exe not found - run tools/build.ps1 first' }
    $logPath = Join-Path $outRoot 'host.ndjson'
    $arguments = @('--level', $Level, '--seed', $Seed, '--port', $Port, '--max-ticks', '400000',
        '--log', $logPath, '--paused')
    if ($Autoplay) { $arguments += @('--autoplay', $Autoplay) }
    $script:hostProcess = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru `
        -WindowStyle Hidden -WorkingDirectory $root
    for ($i = 0; $i -lt 80; $i++) {
        Start-Sleep -Milliseconds 250
        try {
            $null = Invoke-RestMethod -Uri "$base/health" -TimeoutSec 2
            return $true
        } catch {}
    }
    return $false
}

function Get-State {
    try { return Invoke-RestMethod -Uri "$base/state?since=0" -TimeoutSec 5 } catch { return $null }
}

function Get-Levels {
    try { return Invoke-RestMethod -Uri "$base/levels" -TimeoutSec 5 } catch { return $null }
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

# 编辑器整屏截图 -> 识别游戏画布 -> 裁剪出一张 PNG。
#
# 画布位置只在首页识别一次（首页第一张卡片的颜色和位置已知），
# 之后整个会话复用该结果：编辑器窗口不会移动，
# 而游戏画面没有这样的锚点。
function Capture-Raw {
    $raw = Join-Path $root 'temp\capture3_raw.png'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'mcp_call.ps1') `
        -Tool capture_editor_screenshot -ImageOut $raw *> $null
    if (-not (Test-Path -LiteralPath $raw)) { return $null }
    return $raw
}

function Detect-Panel {
    param([switch]$Force)
    if ($script:panel -and -not $Force) {
        return $true
    }
    $raw = Capture-Raw
    if (-not $raw) { return $false }
    $detect = & $python (Join-Path $PSScriptRoot 'find_panel.py') $raw (Join-Path $root 'temp\crop3.json') 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host ("PANEL_INVALID " + ($detect -join ' '))
        return $false
    }
    $script:panel = Read-JsonFile (Join-Path $root 'temp\crop3.json')
    return $true
}

function Save-Shot {
    param([string]$Name)
    $raw = Capture-Raw
    if (-not $raw) { Write-Host ("SHOT_FAILED " + $Name); return $false }
    if (-not (Detect-Panel)) { Write-Host ("SHOT_FAILED " + $Name); return $false }
    $crop = $script:panel
    $target = Join-Path $outRoot $Name
    & $python (Join-Path $PSScriptRoot 'crop_rect.py') $raw $target $crop.x $crop.y $crop.w $crop.h | Out-Null
    if (-not (Test-Path -LiteralPath $target)) { Write-Host ("SHOT_FAILED " + $Name); return $false }
    $state = Get-State
    $entry = [ordered]@{
        file = $Name
        tick = if ($state) { [int]$state.tick } else { -1 }
        collected = if ($state) { [int]$state.hud.fruitsCollected } else { -1 }
        remaining = if ($state) { [int]$state.hud.fruitsRemaining } else { -1 }
        cars = if ($state) { @($state.vehicles).Count } else { -1 }
        status = if ($state) { [string]$state.status } else { 'unknown' }
        crop = [ordered]@{ x = $crop.x; y = $crop.y; w = $crop.w; h = $crop.h }
        bytes = (Get-Item -LiteralPath $target).Length
    }
    $script:shotLog.Add($entry) | Out-Null
    Write-Host ("SHOT " + $Name + " tick=" + $entry.tick + " collected=" + $entry.collected +
        " remaining=" + $entry.remaining + " bytes=" + $entry.bytes)
    return $true
}

# 在已识别的画布内按设计空间（720x1280）坐标点击。
function Click-Design {
    param([double]$DesignX, [double]$DesignY)
    if (-not $script:panel) { return }
    # 截图位图使用设备像素，而系统光标工作在
    # 100%（逻辑）空间，因此两个轴都要除以显示缩放比例。
    $x = [int][Math]::Round(($script:panel.x + $DesignX * $script:panel.w / 720.0) / $DpiScale)
    $y = [int][Math]::Round(($script:panel.y + $DesignY * $script:panel.h / 1280.0) / $DpiScale)
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'click_at.ps1') -X $x -Y $y *> $null
    Start-Sleep -Milliseconds 300
}

function Enter-Level {
    # host 只加载一个关卡，因此目录顶部只有一张卡片：
    # 点击它的中心（任务书 7.4）。点击需要重试，
    # 因为重启后预览需要一点时间安装触摸处理程序；
    # host 的 epoch 是“选择已下发”的判定依据。
    $levels = Get-Levels
    if (-not $levels) { Write-Host 'LEVELS_UNAVAILABLE'; return $false }
    Write-Host ('LEVELS ' + (@($levels.levels).Count) + ' current=' + $levels.current.id)
    $null = Detect-Panel
    if (-not $script:panel) { Write-Host 'PANEL_MISSING'; return $false }
    Write-Host ('PANEL x=' + $script:panel.x + ' y=' + $script:panel.y + ' w=' + $script:panel.w +
        ' h=' + $script:panel.h)
    $before = Get-State
    $script:enterEpoch = if ($before) { [int]$before.levelEpoch } else { 0 }
    $script:enterLevelId = $LevelId
    for ($attempt = 1; $attempt -le 4; $attempt++) {
        Click-Design -DesignX 360 -DesignY 388
        $entered = Wait-For -Condition {
            param($s)
            ([int]$s.levelEpoch -gt $script:enterEpoch) -and ([string]$s.levelId -eq $script:enterLevelId)
        } -TimeoutSeconds 6
        if ($entered) {
            Write-Host ('ENTERED attempt=' + $attempt + ' level=' + (Get-State).levelId +
                ' epoch=' + (Get-State).levelEpoch)
            return $true
        }
        Write-Host ('ENTER_RETRY attempt=' + $attempt)
        Start-Sleep -Milliseconds 800
    }
    Write-Host 'ENTER_FAILED'
    return $false
}

function Set-Paused {
    param([bool]$Paused)
    try {
        $body = '{"paused":' + $Paused.ToString().ToLower() + '}'
        return Invoke-RestMethod -Uri "$base/pause" -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 5
    } catch { return $null }
}

function Write-Index {
    $path = Join-Path $outRoot 'capture_index.json'
    $payload = [ordered]@{
        preset = $Preset
        level = $Level
        levelId = $LevelId
        seed = $Seed
        autoplay = $Autoplay
        generatedAt = (Get-IsoTimestamp)
        panel = $script:panel
        shots = $script:shotLog
    }
    Write-Utf8NoBom -Path $path -Content ($payload | ConvertTo-Json -Depth 8)
    Write-Host ('INDEX ' + $path)
}

switch ($Preset) {
    'curve' {
        Stop-Host
        if (-not (Start-Host)) { Write-Host 'HOST_FAILED'; exit 1 }
        # 先冻结 host，直到客户端进入游戏页面；否则
        # autoplay 驱动会在第一张截图之前就打完这一关。
        $null = Set-Paused $true
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'refresh_preview.ps1') | Out-Null
        Start-Sleep -Seconds 10
        # 先截首页：目录现在共五个关卡（3 个弯道关），这正是
        # 客户端侧“新增关卡纯属数据”的证据。
        $null = Save-Shot 'shot_00_home.png'
        $entered = Enter-Level
        if (-not $entered) { Stop-Host; exit 1 }
        $null = Set-Paused $false
        Start-Sleep -Milliseconds 400
        Save-Shot 'shot_01_curve_initial.png' | Out-Null
        $null = Wait-For -Condition { param($s) [int]$s.hud.fruitsCollected -ge 2 } -TimeoutSeconds 40
        Save-Shot 'shot_02_curve_collecting.png' | Out-Null
        $null = Wait-For -Condition { param($s) [int]$s.hud.fruitsCollected -ge 6 } -TimeoutSeconds 40
        Save-Shot 'shot_03_curve_refilled.png' | Out-Null
        Write-Index
        Stop-Host
    }
    'home' {
        # 含完整五关目录（三个弯道关）的首页：
        # 下半部分通过拖动被遮罩的卡片列表到达。
        Stop-Host
        $Level = 'levels/catalogue.json'
        $exe = Get-BinaryPath -Name 'fc_host'
        $logPath = Join-Path $outRoot 'host_home.ndjson'
        $script:hostProcess = Start-Process -FilePath $exe `
            -ArgumentList @('--catalogue', 'levels/catalogue.json', '--root', '.', '--port', $Port,
                '--max-ticks', '400000', '--log', $logPath, '--paused') `
            -PassThru -WindowStyle Hidden -WorkingDirectory $root
        for ($i = 0; $i -lt 80; $i++) {
            Start-Sleep -Milliseconds 250
            try { $null = Invoke-RestMethod -Uri "$base/health" -TimeoutSec 2; break } catch {}
        }
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'refresh_preview.ps1') | Out-Null
        Start-Sleep -Seconds 10
        if (-not (Detect-Panel)) { Write-Host 'PANEL_MISSING'; Stop-Host; exit 1 }
        $null = Save-Shot 'shot_00_home.png'
        $levels = Get-Levels
        Write-Host ('LEVELS ' + (@($levels.levels).Count) + ' current=' + $levels.current.id)
        # 用屏幕上的箭头（设计坐标 y 930 处的 ▲/▼）翻页，露出
        # 第 3..5 张卡片（即弯道关卡）。本机上合成拖拽无法到达编辑器的
        # Game View，因此按钮才是可靠的途径。
        Click-Design -DesignX 690 -DesignY 930
        Start-Sleep -Milliseconds 700
        $null = Save-Shot 'shot_00b_home_curve_cards.png'
        Write-Index
        Stop-Host
    }
    'burst' {
        # 动画模块（E3）的帧序列：在车辆收集期间尽可能多截几帧，
        # 结算之后再补一帧。
        Stop-Host
        if (-not (Start-Host)) { Write-Host 'HOST_FAILED'; exit 1 }
        # 先冻结 host，直到客户端进入游戏页面；否则
        # autoplay 驱动会在第一张截图之前就打完这一关。
        $null = Set-Paused $true
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'refresh_preview.ps1') | Out-Null
        Start-Sleep -Seconds 10
        $entered = Enter-Level
        if (-not $entered) { Stop-Host; exit 1 }
        $null = Set-Paused $false
        Start-Sleep -Milliseconds 400
        $null = Wait-For -Condition { param($s) [int]$s.hud.fruitsCollected -ge 1 } -TimeoutSeconds 40
        for ($i = 1; $i -le $Frames; $i++) {
            $name = ('frame_{0:00}.png' -f $i)
            Save-Shot $name | Out-Null
            Start-Sleep -Milliseconds $FrameGapMs
        }
        Write-Index
        Stop-Host
    }
    'result' {
        Stop-Host
        if (-not (Start-Host)) { Write-Host 'HOST_FAILED'; exit 1 }
        # 先冻结 host，直到客户端进入游戏页面；否则
        # autoplay 驱动会在第一张截图之前就打完这一关。
        $null = Set-Paused $true
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'refresh_preview.ps1') | Out-Null
        Start-Sleep -Seconds 10
        $entered = Enter-Level
        if (-not $entered) { Stop-Host; exit 1 }
        $null = Set-Paused $false
        Start-Sleep -Milliseconds 400
        $reached = Wait-For -Condition { param($s) $null -ne $s.hud.result } -TimeoutSeconds 180
        Start-Sleep -Milliseconds 700
        Save-Shot 'shot_10_result.png' | Out-Null
        $state = Get-State
        Write-Host ('RESULT reached=' + $reached + ' status=' + $state.hud.result + ' tick=' + $state.hud.resultTick)
        Write-Index
        Stop-Host
    }
    'items' {
        # 预留的技能 UI：道具栏显示快照中的道具，点击会发送 use_item，
        # 其拒绝原因会显示在界面上。
        Stop-Host
        $savedLevel = $Level
        $savedId = $LevelId
        $Level = 'tests/levels/L_curve_items.json'
        $LevelId = 'L_curve_items'
        if (-not (Start-Host)) { Write-Host 'HOST_FAILED'; exit 1 }
        # 先冻结 host，直到客户端进入游戏页面；否则
        # autoplay 驱动会在第一张截图之前就打完这一关。
        $null = Set-Paused $true
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'refresh_preview.ps1') | Out-Null
        Start-Sleep -Seconds 10
        $entered = Enter-Level
        if (-not $entered) { Stop-Host; exit 1 }
        $null = Set-Paused $false
        Start-Sleep -Milliseconds 400
        Save-Shot 'shot_20_items_initial.png' | Out-Null
        # 道具栏的首页位置：画布坐标 (-160, -560) -> 设计坐标 y 1200。
        Click-Design -DesignX 200 -DesignY 1200
        Start-Sleep -Milliseconds 500
        Save-Shot 'shot_21_item_rejected.png' | Out-Null
        $state = Get-State
        Write-Host ('ITEMS ' + (($state.items | ForEach-Object { $_.id + 'x' + $_.charges }) -join ','))
        Write-Index
        Stop-Host
    }
    default {
        Write-Host ('unknown preset ' + $Preset)
        exit 2
    }
}
exit 0
