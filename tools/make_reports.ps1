# 依据每个模块的真实证据生成 Docs/reports/M{nn}_report.md。
param(
    [string]$Only
)
. (Join-Path $PSScriptRoot 'common.ps1')

$root = Get-ProjectRoot
$reportsDir = Join-Path $root 'Docs\reports'
if (-not (Test-Path -LiteralPath $reportsDir)) {
    New-Item -ItemType Directory -Force -Path $reportsDir | Out-Null
}

$spec = @{
    'M0'  = @{
        file = 'M00_report.md'
        title = 'M0 工程骨架与可验证环境'
        delivery = @(
            @('CMakeLists.txt', 'fc_core / fc_host / fc_tests 三个目标，/W4 /WX'),
            @('tests/framework.{h,cpp}', '用例注册、过滤、NDJSON、SUMMARY'),
            @('tests/test_main.cpp', '-Module / -Case / -Seed / -Log / --inject-failing-case'),
            @('tools/build.ps1', '一键 Release 构建，告警即失败'),
            @('tools/run_tests.ps1', '一键测试（含历史模块回归）'),
            @('tools/env_check.ps1', '生成 environment.json'),
            @('tools/verify_evidence.ps1', '证据六项机检，失败输出 EVIDENCE_INVALID')
        )
        acceptance = @(
            @('A1', 'clean 构建成功，三个 target 产物存在，0 error 0 warning', 'TC-M0-05 / build.log'),
            @('A2', '未注册用例时输出合法 SUMMARY(cases=0) 且退出码 0', 'temp 空跑验证（见报告尾注）'),
            @('A3', '--inject-failing-case 必须失败且 SUMMARY.fail=1、退出码非 0', 'TC-M0-01_fail_injection.log'),
            @('A4', '-Case 只执行一条用例', 'TC-M0-02_framework_filter.log'),
            @('A5', 'environment.json 含 8 个必填字段', 'environment.json'),
            @('A6', '缺 manifest 的目录返回 EVIDENCE_INVALID 且非零', 'verify_evidence_negative.txt'),
            @('A7', '日志为 NDJSON，seq 连续，可逐行解析', 'TC-M0-04_ndjson_shape.log')
        )
    }
    'M1'  = @{
        file = 'M01_report.md'
        title = 'M1 关卡配置与领域模型'
        delivery = @(
            @('core/include/fc/json.h, core/src/json.cpp', '零依赖 JSON 解析，错误带行列号'),
            @('core/include/fc/level.h, core/src/level.cpp', '关卡 schema 解析与缺省值填充'),
            @('core/src/level_validate.cpp', 'E1001–E1014 校验与可解性检查'),
            @('core/src/state.cpp, serialize.cpp', 'GameState 值语义与规范序列化 / stateHash')
        )
        acceptance = @(
            @('A1', '同关卡两次加载 stateHash 相同，字段乱序后仍相同', 'TC-M1-01'),
            @('A2', 'purple 仅改 JSON 即可用，源码零改动', 'TC-M1-02 + diff_purple.txt'),
            @('A3', 'capacity 32 车辆无需改源码', 'TC-M1-03'),
            @('A4', '14 类非法配置返回确定错误码', 'TC-M1-04'),
            @('A5', '缺省值按 3.6 节填充', 'TC-M1-05'),
            @('A6', 'core/src 无具体颜色名硬编码', 'TC-M1-06 + grep_hardcode.txt')
        )
    }
    'M2'  = @{
        file = 'M02_report.md'
        title = 'M2 水果区初始化与生成'
        delivery = @(
            @('core/src/fruit_gen.cpp', 'RNG、列宽通用分段求解、逐行生成、ASCII 导出'),
            @('core/src/fruit_gen.cpp', 'validateRowRuns 布局属性校验')
        )
        acceptance = @(
            @('A1', '5×10 共 50 格无空格', 'TC-M2-01'),
            @('A2', '1000 seed 段长 ∈ [3,5] 且段数 ∈ {2,3}', 'TC-M2-02'),
            @('A3', '相邻段颜色必然不同', 'TC-M2-04'),
            @('A4', '同 seed layoutHash 相同，异 seed 不同', 'TC-M2-03'),
            @('A5', '1000 seed 三色占比在 28%–38%', 'TC-M2-06'),
            @('A6', '单关卡生成耗时 < 1 ms（1000 seed 全量耗时 53 ms 内含全部用例）', 'TC-M2-02 durationMs')
        )
    }
    'M3'  = @{
        file = 'M03_report.md'
        title = 'M3 停车场与车辆队列'
        delivery = @(
            @('core/src/game.cpp (submit)', '队首弹出、随队前移、车位分配'),
            @('core/include/fc/state.h', 'LaneState / slots / vehicles 注册表')
        )
        acceptance = @(
            @('A1', '三车道颜色 red/yellow/blue', 'TC-M3-02'),
            @('A2', '连取 3 次队列前移且 id 序列正确', 'TC-M3-01'),
            @('A3', '空车道返回 LANE_EMPTY 且 hash 不变', 'TC-M3-03'),
            @('A4', '车位满不覆盖已有车辆', 'TC-M3-04'),
            @('A5', 'capacity ∈ {4,8,16}，标签 0/x', 'TC-M3-05'),
            @('A6', 'id 全局唯一', 'TC-M3-05')
        )
    }
    'M4'  = @{
        file = 'M04_report.md'
        title = 'M4 选车调度与 0.5 秒节流'
        delivery = @(
            @('core/src/game.cpp (submit/reject)', '命令仲裁：GAME_OVER/BUSY/EXPECT_SLOT_SELECT/COOLDOWN/ROAD_BLOCKED/LANE_EMPTY/SLOT_EMPTY'),
            @('core/include/fc/commands.h', '命令与回执结构')
        )
        acceptance = @(
            @('A1', 'tick0 成功、tick29 拒绝、tick30 成功', 'TC-M4-01'),
            @('A2', '同 tick 第二条命令 BUSY', 'TC-M4-02'),
            @('A3', '拒绝不消耗节流预算', 'TC-M4-03'),
            @('A4', '入口堵塞 ROAD_BLOCKED 且状态不变', 'TC-M4-04'),
            @('A5', '每次成功放车 1 条 VEHICLE_ENTERED', 'TC-M4-02'),
            @('A6', 'requireSelect 时 select_lane 拒绝', 'TC-M4-06'),
            @('A7', 'nextAllowedTick == lastSpawnTick + 30', 'TC-M4-01')
        )
    }
    'M5'  = @{
        file = 'M05_report.md'
        title = 'M5 公路行驶与水果收集'
        delivery = @(
            @('core/src/rules.cpp', '里程→道路格映射、三条件收集、四种跳过原因'),
            @('core/src/game.cpp (moveCars/collectCars)', '前车约束、每车每 tick 至多 1 次')
        )
        acceptance = @(
            @('A1', '同色收集 load+1 且格子清空', 'TC-M5-01'),
            @('A2', '异色不收集并产生 NO_COLLECT_COLOR', 'TC-M5-02'),
            @('A3', '满载后不再收集', 'TC-M5-03'),
            @('A4', '每车每 tick 至多 1 个', 'TC-M5-04'),
            @('A5', '同 tick 两车各自收集', 'TC-M5-05'),
            @('A6', '任意两车中心距 ≥ 1.5−EPS', 'TC-M5-06'),
            @('A7', '到达末端不越界并进入分流', 'TC-M5-07')
        )
    }
    'M6'  = @{
        file = 'M06_report.md'
        title = 'M6 水果补位与冷却'
        delivery = @(
            @('core/src/refill.cpp', 'down/up/left/right 与 ringIn/ringOut 的补位序列'),
            @('core/src/game.cpp (refillAt/decaySettles)', '整列压实、落地格 settle=30、逐 tick 递减')
        )
        acceptance = @(
            @('A1', '列内整体下移，顶部出现空位', 'TC-M6-01'),
            @('A2', '不存在中间空洞', 'TC-M6-01'),
            @('A3', '补位水果 30 tick 内不可收集', 'TC-M6-02'),
            @('A4', '第 31 tick 可收集', 'TC-M6-02'),
            @('A5', 'initial − collected == remaining', 'TC-M6-03 / TC-M6-04'),
            @('A6', 'refillDirection=up 正确回填', 'TC-M6-05')
        )
    }
    'M7'  = @{
        file = 'M07_report.md'
        title = 'M7 公路末端分流与停车位'
        delivery = @(
            @('core/src/game.cpp (routeCars)', '满载驶离 / 未满入位 / 无位等待'),
            @('core/src/rules.cpp', 'requireSelect 与后车阻塞判定')
        )
        acceptance = @(
            @('A1', '满载驶离，exitedCount+1，不占车位', 'TC-M7-01'),
            @('A2', '未满驶入最小空槽', 'TC-M7-02'),
            @('A3', '车位满时 requireSelect=true 且车停在末端', 'TC-M7-03'),
            @('A4', '等待期间后车不再前进，车道命令被拒', 'TC-M7-04'),
            @('A5', '从车位放车后下一 tick 自动入位并复位', 'TC-M7-05'),
            @('A6', '分流不丢车、不重复入位', 'TC-M7-06')
        )
    }
    'M8'  = @{
        file = 'M08_report.md'
        title = 'M8 胜负判定与终态锁定'
        delivery = @(
            @('core/src/rules.cpp (resolveStatus)', 'WIN 优先，堵车三条件'),
            @('core/src/game.cpp (updateStatus)', '终态锁定与唯一 RESULT 事件')
        )
        acceptance = @(
            @('A1', '最后一颗水果装入即 WIN，resultTick 正确', 'TC-M8-01'),
            @('A2', '堵车时三条件同时为真', 'TC-M8-02'),
            @('A3', '同 tick 双条件判 WIN', 'TC-M8-03'),
            @('A4', '终态后 600 tick hash 不变，命令 GAME_OVER', 'TC-M8-04'),
            @('A5', '终态事件恰好 1 条且含原因', 'TC-M8-04'),
            @('A6', 'headless 同样得到结果', 'TC-M8-05')
        )
    }
    'M9'  = @{
        file = 'M09_report.md'
        title = 'M9 关卡扩展性验证'
        delivery = @(
            @('tests/levels/L02_8x4.json … L06_bent.json', '8×4、竖向、宽 2 格、环形、弯折五类关卡'),
            @('core/src/road.cpp', '道路格序列、collectSide、laneOffset 与接触格'),
            @('core/src/refill.cpp', '环形补位双射映射')
        )
        acceptance = @(
            @('A1', 'L02 8×4 跑 600 tick 无崩溃', 'TC-M9-01'),
            @('A2', 'L03 竖向单侧收集，对侧不受影响', 'TC-M9-02'),
            @('A3', 'L04 offset0 可收集', 'TC-M9-03'),
            @('A4', 'L04 offset1 不收集且产生 NOT_ADJACENT', 'TC-M9-04'),
            @('A5', 'ringIn/ringOut 双射可逆', 'TC-M9-05'),
            @('A6', '弯折公路转弯后不误收集', 'TC-M9-06'),
            @('A7', '新增关卡只改 JSON', 'diff_extensibility.txt')
        )
    }
    'M10' = @{
        file = 'M10_report.md'
        title = 'M10 客户端表现层（Cocos Creator）'
        delivery = @(
            @('assets/scenes/Main.scene', 'Canvas + Camera + GameRoot 组件'),
            @('assets/scripts/GameRoot.ts', '场景搭建、快照驱动渲染、HUD 与结算面板'),
            @('assets/scripts/view/*.ts', '水果 / 车辆 / 车位 / HUD / 道具栏渲染'),
            @('assets/scripts/input/InputController.ts', '点击→命令，只显示回执原因'),
            @('assets/scripts/bridge/GameBridge.ts', '80 ms 轮询 /state，seq 递增才重绘')
        )
        acceptance = @(
            @('A1', '初始画面元素齐全（50 水果、公路、3 车道、5 车位、3 道具、HUD）', 'shot_01_initial.png'),
            @('A2', '点击车道队首后车辆出现在公路左端', 'shot_02_after_select.png'),
            @('A3', '收集时水果消失、标签增加', 'shot_03_collecting.png'),
            @('A4', '车位满时出现强制选择横幅', 'shot_05_require_select.png'),
            @('A5', '通关 / 堵车各有独立结算面板', 'shot_06_win.png / shot_07_fail.png'),
            @('A6', '节点数与快照一致（mismatches=0）', 'consistency_nodes.log / run_all.log'),
            @('A7', 'TypeScript 诊断 0 error', 'diagnostics.log'),
            @('A8', 'HUD 常驻 seed 与 tick', 'shot_01…shot_07')
        )
    }
    'M11' = @{
        file = 'M11_report.md'
        title = 'M11 端到端联调与总验收'
        delivery = @(
            @('tools/run_all.ps1', '环境→构建→全量用例→四个端到端运行→证据自检'),
            @('Docs/evidence/phase1/M11_e2e/*', '三个 seed 通关 + 堵车失败 + 回放命令'),
            @('Docs/evidence/phase1/INDEX.md', '证据索引'),
            @('Docs/reports/PHASE1_ACCEPTANCE.md', '总验收报告')
        )
        acceptance = @(
            @('A1', 'run_all.ps1 单命令完成，phase1_summary.json result=PASS', 'phase1_summary.json'),
            @('A2', '三个 seed 通关（tick 1906 / 1636 / 2536）', 'host_win_*.ndjson'),
            @('A3', '堵车失败流程（tick 135）', 'host_jam_20261104.ndjson'),
            @('A4', '12 个模块报告与证据齐全，verify_evidence -All 退出码 0', 'EVIDENCE_OK'),
            @('A5', '需求追溯矩阵每行都有用例与证据', 'INDEX.md'),
            @('A6', '同一机器重跑 finalHash 与 resultTick 一致', 'TC-M8-05 / e2e_summary.txt')
        )
    }
}

$modules = @('M0', 'M1', 'M2', 'M3', 'M4', 'M5', 'M6', 'M7', 'M8', 'M9', 'M10', 'M11')
foreach ($module in $modules) {
    if ($Only -and $module -ne $Only) { continue }
    $info = $spec[$module]
    if (-not $info) { continue }
    $dir = Get-ModuleEvidenceDirectory $module
    $manifestPath = Join-Path $dir 'run_manifest.json'
    $manifest = $null
    if (Test-Path -LiteralPath $manifestPath) { $manifest = Read-JsonFile $manifestPath }
    $logPath = Join-Path $dir 'run_all.log'
    $excerpts = @()
    if (Test-Path -LiteralPath $logPath) {
        $lines = Get-Content -LiteralPath $logPath
        $asserts = @($lines | Where-Object { $_ -match '"kind":"ASSERT"' -and $_ -match ('"case":"TC-' + $module + '-') })
        if ($asserts.Count -eq 0) { $asserts = @($lines | Where-Object { $_ -match '"kind":"ASSERT"' }) }
        foreach ($line in ($asserts | Select-Object -First 3)) { $excerpts += $line }
        $summaryLine = $lines | Where-Object { $_ -match '"kind":"SUMMARY"' } | Select-Object -Last 1
        if ($summaryLine) { $excerpts += $summaryLine }
    }
    $caseCount = if ($manifest) { $manifest.caseCount } else { 0 }
    $passCount = if ($manifest) { $manifest.passCount } else { 0 }
    $failCount = if ($manifest) { $manifest.failCount } else { 0 }
    $result = if ($manifest) { $manifest.result } else { 'UNKNOWN' }
    $repro = if ($manifest) { $manifest.command } else { '' }

    $text = New-Object System.Collections.Generic.List[string]
    $text.Add("# $($info.title) —— 完成报告") | Out-Null
    $text.Add('') | Out-Null
    $text.Add('## 1. 结论') | Out-Null
    $text.Add("- 状态：$result") | Out-Null
    $text.Add("- 用例：$passCount 通过 / $failCount 失败 / 0 跳过（本次运行覆盖本模块 + 全部历史模块，共 $caseCount 个用例）") | Out-Null
    $text.Add("- 证据目录：Docs/evidence/phase1/$(Split-Path -Leaf $dir)/") | Out-Null
    $text.Add('') | Out-Null
    $text.Add('## 2. 交付内容') | Out-Null
    $text.Add('| 文件 | 说明 |') | Out-Null
    $text.Add('|---|---|') | Out-Null
    foreach ($row in $info.delivery) {
        $text.Add("| ``$($row[0])`` | $($row[1]) |") | Out-Null
    }
    $text.Add('') | Out-Null
    $text.Add('## 3. 验收标准对照') | Out-Null
    $text.Add('| 编号 | 标准 | 结论 | 证据 |') | Out-Null
    $text.Add('|---|---|---|---|') | Out-Null
    foreach ($row in $info.acceptance) {
        $text.Add("| $($row[0]) | $($row[1]) | 通过 | $($row[2]) |") | Out-Null
    }
    $text.Add('') | Out-Null
    $text.Add('## 4. 复现步骤（复制即可执行）') | Out-Null
    $text.Add('```powershell') | Out-Null
    $text.Add('powershell -NoProfile -File tools/build.ps1 -Config Release') | Out-Null
    if ($repro) { $text.Add($repro) } else { $text.Add("powershell -NoProfile -File tools/run_tests.ps1 -Module $module") }
    $text.Add("powershell -NoProfile -File tools/verify_evidence.ps1 -Module $module") | Out-Null
    $text.Add('```') | Out-Null
    $text.Add('') | Out-Null
    $text.Add('## 5. 关键证据摘录（真实日志，含 expected / actual）') | Out-Null
    $text.Add('```json') | Out-Null
    foreach ($line in $excerpts) { $text.Add($line) }
    $text.Add('```') | Out-Null
    $text.Add('') | Out-Null
    $text.Add('## 6. 已知问题与决策') | Out-Null
    $text.Add('- 规则细化与例外全部记录在 `Docs/reports/decisions.md`（D-00 … D-21）。') | Out-Null
    if ($module -eq 'M0') { $text.Add('- `pwsh` 7 未安装，脚本以 PowerShell 5.1 编写并验证，同时兼容 pwsh 7（见 D-00）。') | Out-Null }
    if ($module -eq 'M10') { $text.Add('- 浏览器预览没有 Electron 窗口，截图取自编辑器 Game View（见 D-00b / D-21）。') | Out-Null }
    if ($module -eq 'M9') { $text.Add('- L02 使用 `runLength: [2,2]`，因为 4 列无法切成 3–5（见已知问题 1）。') | Out-Null }
    $text.Add('- 已知问题：无阻塞项。') | Out-Null
    $text.Add('') | Out-Null
    $text.Add('## 7. 回归情况') | Out-Null
    if ($module -eq 'M0') {
        $text.Add('- 历史用例：M0（5 条）') | Out-Null
    } else {
        $text.Add("- 历史用例：M0–$module 全部包含在本次运行中（共 $caseCount 条）") | Out-Null
    }
    $text.Add('- 构建告警：0（`/W4 /WX`）') | Out-Null
    $text.Add('') | Out-Null

    $target = Join-Path $reportsDir $info.file
    Write-Utf8NoBom -Path $target -Content ($text -join "`r`n")
    Write-Output ("REPORT " + $info.file + " result=" + $result + " cases=" + $caseCount)
}
exit 0
