@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0scripts\Start-LocalAIRuntime.ps1" (
  echo Local AI Runtime launcher was not found. Please extract the full downloaded ZIP first.
  pause
  exit /b 2
)
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Start-LocalAIRuntime.ps1"
set "LAR_EXIT_CODE=%ERRORLEVEL%"
if not "%LAR_EXIT_CODE%"=="0" (
  echo.
  echo Local AI Runtime could not start. The message above explains what to fix.
  echo You can also open Export diagnostics in the dashboard if the app started.
  pause
)
exit /b %LAR_EXIT_CODE%
