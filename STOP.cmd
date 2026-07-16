@echo off
taskkill /FI "WINDOWTITLE eq llama-embed*" /F >nul 2>&1
taskkill /FI "WINDOWTITLE eq llama-chat*" /F >nul 2>&1
taskkill /IM llama-server.exe /F >nul 2>&1
taskkill /IM compacs-rag.exe /F >nul 2>&1
taskkill /IM main.exe /F >nul 2>&1
echo COMPACS RAG Desktop stopped.
