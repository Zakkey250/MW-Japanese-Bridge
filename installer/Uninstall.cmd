@echo off
setlocal
if "%~1"=="" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall-NFSMWJapaneseBridge.ps1"
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall-NFSMWJapaneseBridge.ps1" -GameDir "%~1"
)
set "RESULT=%ERRORLEVEL%"
echo.
pause
exit /b %RESULT%
