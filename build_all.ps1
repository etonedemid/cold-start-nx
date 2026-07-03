# build_all.ps1 - Build Cold Start for all platforms and bundle into release-out/
# Platforms: Windows, Linux (via WSL archlinux), Switch (via WSL + devkitpro), Android, Wii U (via WSL + devkitpro)
# Usage: .\build_all.ps1 [-SkipWin] [-SkipLinux] [-SkipSwitch] [-SkipAndroid] [-SkipWiiu]

param(
    [switch]$SkipWin,
    [switch]$SkipLinux,
    [switch]$SkipSwitch,
    [switch]$SkipAndroid,
    [switch]$SkipWiiu
)

$ErrorActionPreference = "Stop"

# ── Config ────────────────────────────────────────────────────────────────────
$ROOT    = $PSScriptRoot
$VER     = (Get-Content "$ROOT\source\constants.h" | Select-String 'GAME_VERSION\s*=\s*"([^"]+)"').Matches[0].Groups[1].Value
$OUT     = "$ROOT\release-out"
$WSL     = "archlinux"
$WIN_ZIP = "$OUT\cold_start-windows-$VER.zip"
$LIN_ZIP = "$OUT\cold_start-linux-$VER.zip"
$NRO_OUT = "$OUT\cold-start-nx.nro"
$APK_OUT = "$OUT\cold_start-android-$VER.apk"
$WUHB_OUT = "$OUT\cold_start-wiiu-$VER.wuhb"

Write-Host ""
Write-Host "Cold Start $VER - All-Platform Build" -ForegroundColor White
Write-Host "Output: $OUT" -ForegroundColor Gray
Write-Host ""

New-Item -ItemType Directory -Force -Path $OUT | Out-Null

# ── Helpers ───────────────────────────────────────────────────────────────────
function Banner($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Ok($msg)     { Write-Host "    OK: $msg" -ForegroundColor Green }
function Fail($msg)   { Write-Host "    FAIL: $msg" -ForegroundColor Red; exit 1 }

function Zip-Dir($srcDir, $outZip) {
    Remove-Item $outZip -Force -ErrorAction SilentlyContinue
    Compress-Archive -Path "$srcDir\*" -DestinationPath $outZip -Force
    $sz = [math]::Round((Get-Item $outZip).Length / 1MB, 1)
    Ok "$outZip ($sz MB)"
}

function Wsl-Run($cmd) {
    # $ErrorActionPreference = "Stop" (set above) turns any stderr line from a
    # native command - including ordinary GCC warnings - into a terminating
    # error. Builds routinely warn, so relax to Continue for just this call;
    # callers that care about success already check $LASTEXITCODE explicitly.
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    wsl -d $WSL -- bash -c "$cmd"
    $ErrorActionPreference = $prevEAP
}

# ── Windows ───────────────────────────────────────────────────────────────────
if (-not $SkipWin) {
    Banner "Building Windows..."

    # Kill running instance so linker can overwrite exe
    Get-Process -Name "cold_start" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300

    Set-Location $ROOT
    cmake --build build-win -- -j4
    if ($LASTEXITCODE -ne 0) { Fail "Windows build failed" }

    # Collect: exe + romfs + all needed DLLs
    $winStage = "$env:TEMP\cs_win_stage"
    Remove-Item $winStage -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $winStage | Out-Null
    Copy-Item "$ROOT\build-win\cold_start.exe" $winStage
    Copy-Item "$ROOT\romfs" "$winStage\romfs" -Recurse

    # Bundle every DLL required to run on a clean Windows install by walking
    # the import table transitively (exe -> SDL2/curl -> their own deps, e.g.
    # libcurl->OpenSSL/libssh2, SDL2_ttf->HarfBuzz->Graphite2/Brotli, etc.).
    # A hardcoded list goes stale the moment MSYS2 bumps a package version
    # (versioned filenames like libFLAC-8.dll or libssl-3.dll change), so we
    # resolve recursively from the binary's actual imports instead.
    $mingw = "C:\msys64\mingw64\bin"
    $exePath = "$ROOT\build-win\cold_start.exe"
    if (-not (Get-Command "objdump" -ErrorAction SilentlyContinue)) {
        Fail "objdump not found (expected at $mingw) - cannot resolve DLL dependencies"
    }

    $resolvedDlls = @{}
    $queue = [System.Collections.Generic.Queue[string]]::new()
    $queue.Enqueue($exePath)
    while ($queue.Count -gt 0) {
        $current = $queue.Dequeue()
        $imports = (objdump -p $current 2>$null) | Select-String "DLL Name:" | ForEach-Object {
            ($_ -replace ".*DLL Name:\s*", "").Trim()
        }
        foreach ($dll in $imports) {
            if ($resolvedDlls.ContainsKey($dll)) { continue }
            $src = Join-Path $mingw $dll
            if (Test-Path $src) {
                $resolvedDlls[$dll] = $src
                $queue.Enqueue($src)
            }
            # Not found under mingw64\bin -> assumed to be a Windows system DLL
        }
    }
    foreach ($dll in $resolvedDlls.Keys) {
        Copy-Item $resolvedDlls[$dll] $winStage
    }
    Ok "$($resolvedDlls.Count) DLLs resolved and bundled"

    # Also pick up any DLLs the build placed next to the exe
    Get-ChildItem "$ROOT\build-win\*.dll" -ErrorAction SilentlyContinue | Copy-Item -Destination $winStage

    Zip-Dir $winStage $WIN_ZIP
    Remove-Item $winStage -Recurse -Force
}

# ── Linux ─────────────────────────────────────────────────────────────────────
if (-not $SkipLinux) {
    Banner "Building Linux (WSL: $WSL)..."

    # Job count computed here (not via $(nproc) in Wsl-Run) since that command
    # substitution doesn't survive the PowerShell -> wsl -> bash -c quoting chain.
    Wsl-Run "cmake --build ~/cold-start-nx/build-pc -- -j$($env:NUMBER_OF_PROCESSORS)"
    Wsl-Run '/bin/sh ~/cold-start-nx/build-pc/bundle.sh'
    
    # Remove .log files inside the dist folder (using WSL find)
    Wsl-Run 'find ~/cold-start-nx/build-pc/dist -name ''*.log'' -delete'

    # Zip using PowerShell (no WSL quoting issues). ~/cold-start-nx in WSL is a
    # symlink to this checkout under /mnt/c, so the dist folder is also visible here.
    $distPath = "$ROOT\build-pc\dist"
    if (Test-Path $distPath) {
        Remove-Item $LIN_ZIP -Force -ErrorAction SilentlyContinue
        Compress-Archive -Path "$distPath\*" -DestinationPath $LIN_ZIP -Force
        if (Test-Path $LIN_ZIP) {
            $sz = [math]::Round((Get-Item $LIN_ZIP).Length / 1MB, 1)
            Ok "$LIN_ZIP ($sz MB)"
        } else {
            Fail "PowerShell Compress-Archive failed to create zip"
        }
    } else {
        Fail "dist folder not found at $distPath"
    }
}

# ── Switch ────────────────────────────────────────────────────────────────────
if (-not $SkipSwitch) {
    Banner "Building Switch NRO (WSL: $WSL)..."

    Wsl-Run "/bin/sh ~/cold-start-nx/.build_switch.sh"

    $nroSrc = "$ROOT\cold_start.nro"
    if (-not (Test-Path $nroSrc)) { Fail "cold_start.nro not found after Switch build" }
    Copy-Item $nroSrc $NRO_OUT -Force
    $sz = [math]::Round((Get-Item $NRO_OUT).Length / 1MB, 1)
    Ok "$NRO_OUT ($sz MB)"
}

# ── Wii U ─────────────────────────────────────────────────────────────────────
if (-not $SkipWiiu) {
    wsl -d $WSL -- test -d /opt/devkitpro/wut
    if ($LASTEXITCODE -ne 0) {
        Write-Host "    SKIP: WUT not found at /opt/devkitpro/wut (install via dkp-pacman: wiiu-dev)" -ForegroundColor Yellow
    } else {
        Banner "Building Wii U WUHB (WSL: $WSL)..."

        Wsl-Run "/bin/sh ~/cold-start-nx/.build_wiiu.sh"
        if ($LASTEXITCODE -ne 0) { Fail "Wii U build failed" }

        $wuhbSrc = "$ROOT\cold_start.wuhb"
        if (-not (Test-Path $wuhbSrc)) { Fail "cold_start.wuhb not found after Wii U build" }
        Copy-Item $wuhbSrc $WUHB_OUT -Force
        $sz = [math]::Round((Get-Item $WUHB_OUT).Length / 1MB, 1)
        Ok "$WUHB_OUT ($sz MB)"
    }
}

# ── Android ───────────────────────────────────────────────────────────────────
if (-not $SkipAndroid) {
    Banner "Building Android APK..."

    # Android build uses WSL (symlinks in CMakeLists.txt require Linux)
    Wsl-Run "sh ~/cold-start-nx/.build_android.sh"
    if ($LASTEXITCODE -ne 0) { Fail "Android build failed" }

    # Sign and write directly to release-out
    $apkWsl = '/mnt/' + $APK_OUT.Substring(0,1).ToLower() + ($APK_OUT.Substring(2) -replace '\\', '/')
    Wsl-Run "bash ~/cold-start-nx/android/sign_apk.sh '$apkWsl'"
    if ($LASTEXITCODE -ne 0) { Fail "APK signing failed" }
    if (-not (Test-Path $APK_OUT)) { Fail "Signed APK not found at $APK_OUT" }
    $sz = [math]::Round((Get-Item $APK_OUT).Length / 1MB, 1)
    Ok "$APK_OUT ($sz MB)"
}

# ── Summary ───────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "release-out/ contents:" -ForegroundColor White
Get-ChildItem $OUT | ForEach-Object {
    $sz = [math]::Round($_.Length / 1MB, 1)
    Write-Host ("  {0,-45} {1,6} MB" -f $_.Name, $sz)
}
Write-Host ""
Write-Host "Done. Cold Start $VER ready for release." -ForegroundColor Green
