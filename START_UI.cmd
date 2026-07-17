@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title COMPACS RAG Desktop (WebView UI)

set "ROOT=%CD%"

if exist "%ROOT%\build\Release\main.exe" (
  set "APPDIR=%ROOT%\build\Release"
) else if exist "%ROOT%\main.exe" (
  set "APPDIR=%ROOT%"
) else (
  echo ERROR: main.exe not found. Run build.bat or unpack Part1.
  pause
  exit /b 1
)

set "LLAMA=%ROOT%\llama\llama-server.exe"
set "EMBED=%ROOT%\models\nomic-embed-text.gguf"
set "CHAT=%ROOT%\models\llama3.2-3b-instruct-q4_K_M.gguf"
set "NUM_CTX=8192"

if exist "%APPDIR%\config.yaml" (
  for /f "usebackq tokens=2 delims=: " %%V in (`findstr /R /C:"num_ctx:" "%APPDIR%\config.yaml"`) do set "NUM_CTX=%%V"
) else if exist "%ROOT%\config.yaml" (
  for /f "usebackq tokens=2 delims=: " %%V in (`findstr /R /C:"num_ctx:" "%ROOT%\config.yaml"`) do set "NUM_CTX=%%V"
)

if not exist "%LLAMA%" (
  echo ERROR: missing llama\llama-server.exe
  pause
  exit /b 1
)
if not exist "%CHAT%" (
  echo ERROR: missing models\llama3.2-3b-instruct-q4_K_M.gguf — unpack Part2
  pause
  exit /b 1
)
if not exist "%EMBED%" (
  echo ERROR: missing models\nomic-embed-text.gguf — unpack Part3
  pause
  exit /b 1
)
if not exist "%APPDIR%\vectors.bin" (
  if exist "%ROOT%\vectors.bin" (
    copy /Y "%ROOT%\vectors.bin" "%APPDIR%\vectors.bin" >nul
  ) else (
    echo ERROR: vectors.bin not found next to main.exe ^(%APPDIR%^)
    pause
    exit /b 1
  )
)

echo Using APPDIR=%APPDIR%
for %%P in (8081 8082) do (
  for /f "tokens=5" %%A in ('netstat -ano ^| findstr /R /C:":%%P .*LISTENING"') do taskkill /PID %%A /F >nul 2>&1
)

start "llama-embed" /D "%ROOT%\llama" cmd /c ""%LLAMA%" -m "%EMBED%" --embeddings --pooling mean --host 127.0.0.1 --port 8081"
start "llama-chat" /D "%ROOT%\llama" cmd /c ""%LLAMA%" -m "%CHAT%" --host 127.0.0.1 --port 8082 -c %NUM_CTX%"

powershell -NoProfile -Command "$ok=$false; 1..90 | ForEach-Object { try { $e=(Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8081/health -TimeoutSec 2).StatusCode -eq 200; $c=(Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8082/health -TimeoutSec 2).StatusCode -eq 200; if($e -and $c){$ok=$true; break} } catch {}; Start-Sleep 2 }; if(-not $ok){ exit 1 }"

if errorlevel 1 (
  echo ERROR: llama-server did not become ready
  pause
  exit /b 1
)

start "COMPACS-RAG" /D "%APPDIR%" "%APPDIR%\main.exe"
echo Ready: http://127.0.0.1:8765
exit /b 0
