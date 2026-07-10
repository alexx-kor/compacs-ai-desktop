@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title COMPACS RAG Desktop

set "APP=compacs-rag.exe"
if not exist "%APP%" set "APP=main.exe"
if not exist "%APP%" (
  echo ERROR: compacs-rag.exe / main.exe not found in %CD%
  pause
  exit /b 1
)
if not exist "llama\llama-server.exe" (
  echo ERROR: missing llama\llama-server.exe
  pause
  exit /b 1
)

rem Desktop needs localhost:8081 for embed llama-server (8082 for chat).
for /f "tokens=5" %%P in ('netstat -ano ^| findstr /R /C:":8081 .*LISTENING"') do set "PID8081=%%P"
if defined PID8081 (
  echo.
  echo WARNING: port 8081 is already in use (PID %PID8081%).
  echo Desktop embed server cannot start.
  echo.
  pause
)

echo Starting COMPACS RAG via %APP% ...
echo First launch may take 1-2 minutes.
start "" /D "%CD%" "%APP%"
goto :eof
