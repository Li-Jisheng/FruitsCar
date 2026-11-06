@echo off
rem 以 Release 配置构建 fc_core / fc_host / fc_tests（/W4 /WX）。
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build.ps1" -Config Release %*
endlocal
