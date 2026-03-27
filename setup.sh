#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

echo "===================================================="
echo "  RecruitmentTracker - Dependency Setup (Unix)"
echo "===================================================="

mkdir -p third_party

echo "[1/3] Downloading cpp-httplib..."
curl -sL "https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.15.3/httplib.h" \
     -o "third_party/httplib.h"
echo "      OK: third_party/httplib.h"

echo "[2/3] Downloading nlohmann/json..."
curl -sL "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" \
     -o "third_party/json.hpp"
echo "      OK: third_party/json.hpp"

echo "[3/3] Downloading SQLite3 amalgamation..."
curl -sL "https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip" \
     -o "third_party/sqlite.zip"
cd third_party
unzip -q sqlite.zip
cp sqlite-amalgamation-3450100/sqlite3.h .
cp sqlite-amalgamation-3450100/sqlite3.c .
rm -rf sqlite.zip sqlite-amalgamation-3450100
cd ..
echo "      OK: third_party/sqlite3.h + sqlite3.c"

echo ""
echo "===================================================="
echo "  Build:"
echo "    mkdir build && cd build"
echo "    cmake .. -DCMAKE_BUILD_TYPE=Release"
echo "    cmake --build ."
echo "    ./recruitment_tracker"
echo ""
echo "  Open: http://localhost:8080"
echo "  Login: admin / admin123"
echo "===================================================="
