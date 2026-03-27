# ============================================================
#  start.ps1  —  Launch the recruitment tracker server
#
#  Logs (NDJSON, one JSON object per line) are written to
#  app.log in the project root.  The script tails the log
#  in real time so you see structured output in the console.
#
#  Optional: set the minimum log level before running
#    $env:LOG_LEVEL = "DEBUG"   # DEBUG / INFO / WARN / ERROR
#    .\start.ps1
# ============================================================

$ErrorActionPreference = "Stop"

$exePath  = Join-Path $PSScriptRoot "build\recruitment_tracker.exe"
$logFile  = Join-Path $PSScriptRoot "app.log"

if (-not (Test-Path $exePath)) {
    Write-Error "Executable not found at $exePath — run the build first."
    exit 1
}

# Ensure the log file exists so Get-Content -Wait can tail it
if (-not (Test-Path $logFile)) { New-Item $logFile -ItemType File | Out-Null }

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "  Interview Scheduler" -ForegroundColor Cyan
Write-Host "  Browser  : http://localhost:8080" -ForegroundColor Cyan
Write-Host "  Log file : $logFile" -ForegroundColor Cyan
Write-Host "  Log level: $($env:LOG_LEVEL ?? 'INFO')" -ForegroundColor Cyan
Write-Host "  Press Ctrl+C to stop." -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host ""

# Start the server — stderr goes to the log file (append)
$proc = Start-Process `
    -FilePath    $exePath `
    -NoNewWindow `
    -RedirectStandardError $logFile `
    -PassThru

Write-Host "Server PID: $($proc.Id)" -ForegroundColor Green
Write-Host ""

# Tail the log in real time, coloring lines by level
try {
    Get-Content $logFile -Wait | ForEach-Object {
        $line = $_
        if     ($line -match '"level":"ERROR"') { Write-Host $line -ForegroundColor Red     }
        elseif ($line -match '"level":"WARN"')  { Write-Host $line -ForegroundColor Yellow  }
        elseif ($line -match '"level":"DEBUG"') { Write-Host $line -ForegroundColor DarkGray }
        else                                    { Write-Host $line -ForegroundColor White   }
    }
} finally {
    # Ctrl+C lands here — stop the server gracefully
    if (-not $proc.HasExited) {
        Write-Host "`nStopping server (PID $($proc.Id))..." -ForegroundColor Yellow
        $proc.Kill()
    }
}
