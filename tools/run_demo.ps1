# 启动本地 FruitsCar host，并打印后续操作说明。
#
# Cocos 客户端是纯视图：它只渲染 host 在
# 127.0.0.1:8787 上报的内容。没有 host 时棋盘按设计为空。
#
#   powershell -File tools/run_demo.ps1                 # 单关卡，手动游玩
#   powershell -File tools/run_demo.ps1 -All            # 五关目录
#   powershell -File tools/run_demo.ps1 -All -Mode greedy   # autoplay 演示
param(
    [string]$Level = 'tests/levels/L01_default.json',
    [int]$Seed = 20261104,
    [string]$Mode = '',
    [int]$Port = 8787,
    [switch]$All,
    [switch]$Wait
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$hostExe = Get-BinaryPath -Name 'fc_host'
if (-not $hostExe) {
    Write-Output 'fc_host.exe not found - building first...'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build.ps1') | Out-Null
    $hostExe = Get-BinaryPath -Name 'fc_host'
    if (-not $hostExe) {
        Write-Output 'RUN_DEMO_FAILED build failed'
        exit 1
    }
}

# 同一端口只能由一个 host 占用。
foreach ($stale in (Get-Process fc_host -ErrorAction SilentlyContinue)) {
    Stop-Process -Id $stale.Id -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 400

$logPath = Join-Path $root 'temp\demo_host.ndjson'
$arguments = @()
if ($All) {
    # 五关目录：客户端为每个关卡显示一张卡片（2 个网格关 + 3 个弯道关）。
    $arguments += @('--catalogue', 'levels/catalogue.json', '--root', '.')
} else {
    $arguments += @('--level', $Level)
}
$arguments += @('--seed', $Seed, '--port', $Port, '--log', $logPath, '--max-ticks', '2000000')
if ($Mode) { $arguments += @('--autoplay', $Mode) }
$process = Start-Process -FilePath $hostExe -ArgumentList $arguments -PassThru -WindowStyle Hidden `
    -WorkingDirectory $root

$ready = $false
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250
    try {
        $health = Invoke-RestMethod -Uri ("http://127.0.0.1:" + $Port + "/health") -TimeoutSec 2
        if ($health.ok) { $ready = $true; break }
    } catch {
    }
}
if (-not $ready) {
    Write-Output 'RUN_DEMO_FAILED host did not answer /health'
    exit 1
}

$levels = Invoke-RestMethod -Uri ("http://127.0.0.1:" + $Port + "/levels") -TimeoutSec 3
$levelInfo = Invoke-RestMethod -Uri ("http://127.0.0.1:" + $Port + "/level") -TimeoutSec 3
Write-Output ('HOST_OK url=http://127.0.0.1:' + $Port + ' levels=' + @($levels.levels).Count +
    ' current=' + $levelInfo.id + ' seed=' + $Seed + ' mode=' + $(if ($Mode) { $Mode } else { 'manual' }))
Write-Output 'Now press the Play button in Cocos Creator (preview) - the board is fed by this host.'
Write-Output ('State : http://127.0.0.1:' + $Port + '/state')
Write-Output ('Log   : ' + $logPath)
Write-Output 'Stop  : Get-Process fc_host | Stop-Process -Force'

if ($Wait) {
    while (-not $process.HasExited) {
        Start-Sleep -Seconds 2
        try {
            $state = Invoke-RestMethod -Uri ("http://127.0.0.1:" + $Port + "/state?since=0") -TimeoutSec 2
            Write-Output ('tick=' + $state.tick + ' fruits=' + $state.hud.fruitsRemaining + '/' + $state.hud.fruitsTotal +
                ' cars=' + $state.vehicles.Count + ' status=' + $state.status)
        } catch {
            Write-Output 'host stopped'
            break
        }
    }
}
exit 0
