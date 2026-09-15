@echo off
setlocal
if "%~1"=="" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-NFSMWJapaneseBridge.ps1" -UseExistingJapaneseResources
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-NFSMWJapaneseBridge.ps1" -GameDir "%~1" -UseExistingJapaneseResources
)
set "RESULT=%ERRORLEVEL%"
echo.
pause
exit /b %RESULT%
