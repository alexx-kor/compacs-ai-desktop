@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title COMPACS RAG Desktop (WebView UI)

set "LLAMA=llama\llama-server.exe"
set "EMBED=models\nomic-embed-text.gguf"
set "CHAT=models\llama3.2-3b-instruct-q4_K_M.gguf"
set "NUM_CTX=16384"

if exist "config.yaml" (
  for /f "usebackq tokens=2 delims=: " %%V in (`findstr /R /C:"num_ctx:" config.yaml`) do set "NUM_CTX=%%V"
)

for %%P in (8081 8082) do (
  for /f "tokens=5" %%A in ('netstat -ano ^| findstr /R /C:":%%P .*LISTENING"') do taskkill /PID %%A /F >nul 2>&1
)

start "llama-embed" /D "%CD%\llama" cmd /c ""%CD%\%LLAMA%" -m "%CD%\%EMBED%" --embeddings --pooling mean --host 127.0.0.1 --port 8081"
start "llama-chat" /D "%CD%\llama" cmd /c ""%CD%\%LLAMA%" -m "%CD%\%CHAT%" --host 127.0.0.1 --port 8082 -c %NUM_CTX%"

powershell -NoProfile -Command "$ok=$false; 1..90 | ForEach-Object { try { $e=(Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8081/health -TimeoutSec 2).StatusCode -eq 200; $c=(Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8082/health -TimeoutSec 2).StatusCode -eq 200; if($e -and $c){$ok=$true; break} } catch {}; Start-Sleep 2 }; if(-not $ok){ exit 1 }"

start "COMPACS-RAG" /D "%CD%" main.exe
echo Ready: http://127.0.0.1:8765
exit /b 0
