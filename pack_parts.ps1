# Pack COMPACS Desktop into Part1/2/3 (< 2 GB each).
# Usage: powershell -ExecutionPolicy Bypass -File .\pack_parts.ps1

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Dist = Join-Path $Root "dist"
$Stage = Join-Path $Dist "_stage"

Write-Host "Root: $Root"
Write-Host "Cleaning dist stage..."

if (Test-Path $Dist) {
    Remove-Item $Dist -Recurse -Force
}
New-Item -ItemType Directory -Path $Stage -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $Stage "Part1") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $Stage "Part2\models") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $Stage "Part3\models") -Force | Out-Null

function Require-File([string]$Path) {
    if (-not (Test-Path $Path)) {
        throw "Missing required file: $Path"
    }
}

$Release = Join-Path $Root "build\Release"
Require-File (Join-Path $Release "main.exe")
Require-File (Join-Path $Release "config.yaml")
Require-File (Join-Path $Release "vectors.bin")
Require-File (Join-Path $Release "assets\index.html")
Require-File (Join-Path $Root "llama\llama-server.exe")
Require-File (Join-Path $Root "models\llama3.2-3b-instruct-q4_K_M.gguf")
Require-File (Join-Path $Root "models\nomic-embed-text.gguf")

$Part1 = Join-Path $Stage "Part1"

Write-Host "Staging Part1 (App)..."
Copy-Item (Join-Path $Release "main.exe") $Part1 -Force
if (Test-Path (Join-Path $Release "export_vectors.exe")) {
    Copy-Item (Join-Path $Release "export_vectors.exe") $Part1 -Force
}
Copy-Item (Join-Path $Release "config.yaml") $Part1 -Force
Copy-Item (Join-Path $Release "vectors.bin") $Part1 -Force
Copy-Item (Join-Path $Release "assets") (Join-Path $Part1 "assets") -Recurse -Force
Copy-Item (Join-Path $Root "llama") (Join-Path $Part1 "llama") -Recurse -Force
Copy-Item (Join-Path $Root "README.md") $Part1 -Force
Copy-Item (Join-Path $Root "ASSEMBLE.md") $Part1 -Force
Copy-Item (Join-Path $Root "STOP.cmd") $Part1 -Force

New-Item -ItemType Directory -Path (Join-Path $Part1 "models") -Force | Out-Null
@"
Place model files from Part2 and Part3 into this folder:

  llama3.2-3b-instruct-q4_K_M.gguf   (Part2)
  nomic-embed-text.gguf              (Part3)

See ASSEMBLE.md
"@ | Set-Content -Path (Join-Path $Part1 "models\README.txt") -Encoding UTF8

@"
@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title COMPACS RAG Desktop

if not exist "main.exe" (
  echo ERROR: main.exe not found in %CD%
  pause
  exit /b 1
)
if not exist "llama\llama-server.exe" (
  echo ERROR: missing llama\llama-server.exe
  pause
  exit /b 1
)
if not exist "models\llama3.2-3b-instruct-q4_K_M.gguf" (
  echo ERROR: missing models\llama3.2-3b-instruct-q4_K_M.gguf — unpack Part2 here
  pause
  exit /b 1
)
if not exist "models\nomic-embed-text.gguf" (
  echo ERROR: missing models\nomic-embed-text.gguf — unpack Part3 here
  pause
  exit /b 1
)

echo Starting COMPACS RAG Desktop...
echo Ensure llama-server is running on 127.0.0.1:8081
start "" /D "%CD%" "main.exe"
goto :eof
"@ | Set-Content -Path (Join-Path $Part1 "START.cmd") -Encoding ASCII

Write-Host "Staging Part2 (Chat model)..."
Copy-Item (Join-Path $Root "models\llama3.2-3b-instruct-q4_K_M.gguf") (Join-Path $Stage "Part2\models\") -Force

Write-Host "Staging Part3 (Embed model)..."
Copy-Item (Join-Path $Root "models\nomic-embed-text.gguf") (Join-Path $Stage "Part3\models\") -Force

function Zip-Folder([string]$SourceDir, [string]$ZipPath) {
    if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
    Write-Host "Compressing $ZipPath ..."
    Compress-Archive -Path (Join-Path $SourceDir "*") -DestinationPath $ZipPath -CompressionLevel Optimal
    $len = (Get-Item $ZipPath).Length
    Write-Host ("  -> {0:N1} MB" -f ($len / 1MB))
    if ($len -ge 2GB) {
        throw "Archive exceeds 2 GB: $ZipPath"
    }
}

$zip1 = Join-Path $Dist "COMPACS-Desktop-Part1-App.zip"
$zip2 = Join-Path $Dist "COMPACS-Desktop-Part2-ChatModel.zip"
$zip3 = Join-Path $Dist "COMPACS-Desktop-Part3-EmbedModel.zip"

Zip-Folder (Join-Path $Stage "Part1") $zip1
Zip-Folder (Join-Path $Stage "Part2") $zip2
Zip-Folder (Join-Path $Stage "Part3") $zip3

Copy-Item (Join-Path $Root "ASSEMBLE.md") (Join-Path $Dist "ASSEMBLE.md") -Force

Write-Host ""
Write-Host "Done. Output in: $Dist"
Get-ChildItem $Dist -Filter "*.zip" | ForEach-Object {
    "{0,-45} {1,10:N1} MB" -f $_.Name, ($_.Length / 1MB)
}
Write-Host ""
Write-Host "Stage folders left under dist\_stage (optional cleanup)."
