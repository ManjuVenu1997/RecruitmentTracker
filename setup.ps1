# setup.ps1 - Download all dependencies for RecruitmentTracker
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host "====================================================" -ForegroundColor Cyan
Write-Host "  RecruitmentTracker - Dependency Setup (PowerShell)" -ForegroundColor Cyan
Write-Host "====================================================" -ForegroundColor Cyan

if (!(Test-Path "third_party")) { New-Item -ItemType Directory "third_party" | Out-Null }

# Trust all SSL certs for this session (handles corporate proxies)
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12

Write-Host "[1/3] Downloading cpp-httplib..." -ForegroundColor Yellow
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.15.3/httplib.h" `
    -OutFile "third_party\httplib.h" -UseBasicParsing
Write-Host "      OK: third_party\httplib.h" -ForegroundColor Green

Write-Host "[2/3] Downloading nlohmann/json..." -ForegroundColor Yellow
Invoke-WebRequest -Uri "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" `
    -OutFile "third_party\json.hpp" -UseBasicParsing
Write-Host "      OK: third_party\json.hpp" -ForegroundColor Green

Write-Host "[3/3] Downloading SQLite3 amalgamation..." -ForegroundColor Yellow
Invoke-WebRequest -Uri "https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip" `
    -OutFile "third_party\sqlite.zip" -UseBasicParsing

Expand-Archive "third_party\sqlite.zip" -DestinationPath "third_party\sqlite_tmp" -Force
Copy-Item "third_party\sqlite_tmp\sqlite-amalgamation-3450100\sqlite3.h" "third_party\sqlite3.h"
Copy-Item "third_party\sqlite_tmp\sqlite-amalgamation-3450100\sqlite3.c" "third_party\sqlite3.c"
Remove-Item "third_party\sqlite.zip"
Remove-Item "third_party\sqlite_tmp" -Recurse
Write-Host "      OK: third_party\sqlite3.h + sqlite3.c" -ForegroundColor Green

Write-Host ""
Write-Host "====================================================" -ForegroundColor Cyan
Write-Host "  Dependencies ready!  Now build:" -ForegroundColor Cyan
Write-Host ""
Write-Host "    mkdir build" -ForegroundColor White
Write-Host "    cd build" -ForegroundColor White
Write-Host "    cmake .. -DCMAKE_BUILD_TYPE=Release" -ForegroundColor White
Write-Host "    cmake --build . --config Release" -ForegroundColor White
Write-Host ""
Write-Host "  Run from build\Release\ :" -ForegroundColor White
Write-Host "    .\recruitment_tracker.exe" -ForegroundColor White
Write-Host ""
Write-Host "  Open browser: http://localhost:8080" -ForegroundColor Green
Write-Host "  Credentials:  admin / admin123" -ForegroundColor Green
Write-Host "====================================================" -ForegroundColor Cyan
