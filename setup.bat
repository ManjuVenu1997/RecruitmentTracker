@echo off
setlocal EnableDelayedExpansion

echo ============================================
echo  RecruitmentTracker - Dependency Setup
echo ============================================
echo.

if not exist "third_party" mkdir third_party

echo [1/3] Downloading cpp-httplib (single header HTTP server)...
curl -sL "https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.15.3/httplib.h" -o "third_party\httplib.h"
if %errorlevel% neq 0 (
    echo ERROR: Failed to download httplib.h
    echo Please download manually from: https://github.com/yhirose/cpp-httplib/releases
    exit /b 1
)
echo     OK: third_party\httplib.h

echo [2/3] Downloading nlohmann/json (single header JSON library)...
curl -sL "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" -o "third_party\json.hpp"
if %errorlevel% neq 0 (
    echo ERROR: Failed to download json.hpp
    echo Please download manually from: https://github.com/nlohmann/json/releases
    exit /b 1
)
echo     OK: third_party\json.hpp

echo [3/3] Downloading SQLite3 amalgamation...
curl -sL "https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip" -o "third_party\sqlite.zip"
if %errorlevel% neq 0 (
    echo ERROR: Failed to download SQLite3
    echo Please download sqlite-amalgamation from: https://sqlite.org/download.html
    exit /b 1
)
cd third_party
tar -xf sqlite.zip 2>nul
copy /Y "sqlite-amalgamation-3450100\sqlite3.h" "sqlite3.h" >nul
copy /Y "sqlite-amalgamation-3450100\sqlite3.c" "sqlite3.c" >nul
del sqlite.zip
rmdir /S /Q sqlite-amalgamation-3450100 2>nul
cd ..
echo     OK: third_party\sqlite3.h + sqlite3.c

echo.
echo ============================================
echo  Dependencies ready! Now build:
echo.
echo    mkdir build
echo    cd build
echo    cmake .. -DCMAKE_BUILD_TYPE=Release
echo    cmake --build . --config Release
echo.
echo  Then run from the build folder:
echo    .\Release\recruitment_tracker.exe
echo  (or .\recruitment_tracker on Linux)
echo.
echo  Open browser: http://localhost:8080
echo  Default login  admin / admin123
echo              recruiter1 / admin123
echo              interviewer1 / admin123
echo              interviewer2 / admin123
echo ============================================
