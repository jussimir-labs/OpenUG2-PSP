@echo off
rem Windows: dois cliques neste arquivo. A conversao roda no WSL (o Linux do
rem Windows): instale-o uma vez com "wsl --install" num PowerShell de
rem administrador e reinicie. Arraste a ISO sobre este arquivo, ou ele pergunta.
setlocal
cd /d "%~dp0"
where wsl >nul 2>nul
if errorlevel 1 (
  echo Falta o WSL. Num PowerShell de administrador, rode: wsl --install
  echo Reinicie o computador e abra este arquivo de novo.
  pause
  exit /b 1
)
set "ISO=%~1"
if "%ISO%"=="" set /p "ISO=Arraste a ISO para esta janela e tecle Enter: "
set "ISO=%ISO:"=%"
rem --exec: no shell in between, so the paths' spaces and backslashes stay
for /f "usebackq delims=" %%i in (`wsl --exec wslpath -a "%CD%"`) do set "HERE_WSL=%%i"
for /f "usebackq delims=" %%i in (`wsl --exec wslpath -a "%ISO%"`) do set "ISO_WSL=%%i"
wsl --cd "%HERE_WSL%" --exec sh ./gerar-jogo.sh "%ISO_WSL%"
if errorlevel 1 (
  echo.
  echo Nao terminou. Veja a mensagem acima.
) else (
  start "" "%CD%\saida"
)
pause
