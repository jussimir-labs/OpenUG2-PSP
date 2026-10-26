@echo off
rem Open the browser generator using the existing Windows WSL environment.
setlocal
cd /d "%~dp0"
where wsl >nul 2>nul
if errorlevel 1 (
  echo Windows WSL is required for the converters.
  echo Install it once: https://learn.microsoft.com/windows/wsl/install
  pause
  exit /b 1
)
for /f "usebackq delims=" %%i in (`wsl --exec wslpath -a "%CD%"`) do set "HERE_WSL=%%i"
if not defined HERE_WSL (
  echo Could not find the WSL distribution. Open WSL once to finish setup.
  pause
  exit /b 1
)
wsl --cd "%HERE_WSL%" --exec sh ./generate-game.sh
if errorlevel 1 pause
