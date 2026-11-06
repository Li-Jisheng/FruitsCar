@echo off
rem 完整回归：环境检查 -> 构建 -> 全部测试 -> 端到端 -> 证据校验。
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run_all.ps1" %*
endlocal
