@echo off
rem 启动本地 FruitsCar 宿主（127.0.0.1:8787），不修改本机的 PowerShell 执行策略。
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run_demo.ps1" %*
endlocal
