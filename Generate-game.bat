@echo off
setlocal
cd /d "%~dp0"
if not exist nfsu2-generator.exe (
 echo Download and extract the complete native Windows package from the beta release.
 pause
 exit /b 1
)
nfsu2-generator.exe serve
if errorlevel 1 pause
