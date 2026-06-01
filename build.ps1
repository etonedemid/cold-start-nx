# build.ps1 - reliable build wrapper for cold_start
# Kills any running instance, then builds. Retries once if the linker
# loses a race with Windows Defender scanning the output exe.

param(
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

# Kill running game instance so the linker can overwrite the exe
$proc = Get-Process -Name "cold_start" -ErrorAction SilentlyContinue
if ($proc) {
    Write-Host "Stopping running cold_start.exe (PID $($proc.Id))..."
    $proc | Stop-Process -Force
    Start-Sleep -Milliseconds 400
}

# Wait until the exe is no longer locked (Defender scan / slow shutdown)
$exePath = Join-Path $PSScriptRoot "build-win\cold_start.exe"
if (Test-Path $exePath) {
    $waited = 0
    while ($waited -lt 5000) {
        try {
            $f = [IO.File]::Open($exePath, 'Open', 'ReadWrite', 'None')
            $f.Close()
            break
        } catch {
            Start-Sleep -Milliseconds 200
            $waited += 200
        }
    }
}

function Run-Build {
    cmake --build build-win --config $Config
    return $LASTEXITCODE
}

Write-Host "Building..."
$exit = Run-Build

if ($exit -ne 0) {
    Write-Host "Build failed (exit $exit) - waiting 1s for file lock to clear, then retrying..."
    Start-Sleep -Seconds 1
    $exit = Run-Build
}

if ($exit -eq 0) {
    Write-Host "Build succeeded."
} else {
    Write-Host "Build failed after retry. Check output above."
    exit $exit
}
