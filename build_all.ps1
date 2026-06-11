# build_all.ps1 - Build Cold Start for all platforms and bundle into release-out/
# Platforms: Windows, Linux (via WSL archlinux), Switch (via WSL + devkitpro), Android
# Usage: .\build_all.ps1 [-SkipWin] [-SkipLinux] [-SkipSwitch] [-SkipAndroid]

param(
    [switch]$SkipWin,
    [switch]$SkipLinux,
    [switch]$SkipSwitch,
    [switch]$SkipAndroid
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
    wsl -d $WSL -- bash -c "$cmd"   # ← added double quotes
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

    # Bundle all DLLs required to run on a clean Windows install.
    # List covers DLLs pulled in transitively (SDL2_ttf→HarfBuzz→Graphite2,
    # FreeType→Brotli; SDL2_mixer→mpg123/opus/FLAC; libcurl→OpenSSL/libssh2).
    $mingw = "C:\msys64\mingw64\bin"
    $neededDlls = @(
        # GCC/MinGW runtime
        "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll",
        # SDL2 family
        "SDL2.dll", "SDL2_image.dll", "SDL2_mixer.dll", "SDL2_ttf.dll",
        # SDL2_image deps
        "libjpeg-8.dll", "libpng16-16.dll", "zlib1.dll", "libbz2-1.dll",
        "libwebp-7.dll", "libwebpdecoder-3.dll", "libwebpdemux-2.dll", "libwebpmux-3.dll",
        # SDL2_mixer deps
        "libogg-0.dll", "libvorbis-0.dll", "libvorbisfile-3.dll",
        "libopus-0.dll", "libopusfile-0.dll",
        "libmpg123-0.dll",
        "libFLAC-8.dll", "libFLAC-12.dll",
        "libmodplug-1.dll",
        # SDL2_ttf deps (HarfBuzz text shaping + FreeType WOFF2 via Brotli)
        "libfreetype-6.dll",
        "libharfbuzz-0.dll", "libgraphite2.dll",
        "libbrotlidec.dll", "libbrotlicommon.dll",
        # Workshop / updates (curl + TLS)
        "libcurl-4.dll",
        "libssl-3.dll", "libcrypto-3.dll",
        "libssl-1_1-x64.dll", "libcrypto-1_1-x64.dll",
        "libssh2-1.dll",
        # Multiplayer UPnP
        "libminiupnpc.dll"
    )
    foreach ($dll in $neededDlls) {
        $src = Join-Path $mingw $dll
        if (Test-Path $src) { Copy-Item $src $winStage }
        # Missing DLLs are silently skipped — some are optional or version-specific
    }

    # Auto-detect any additional imports the exe directly needs (requires objdump)
    $exePath = "$ROOT\build-win\cold_start.exe"
    if ((Get-Command "objdump" -ErrorAction SilentlyContinue) -and (Test-Path $exePath)) {
        $autoDlls = (objdump -p $exePath 2>$null) | Select-String "DLL Name:" | ForEach-Object {
            ($_ -replace ".*DLL Name:\s*", "").Trim()
        }
        foreach ($dll in $autoDlls) {
            $src = Join-Path $mingw $dll
            if ((Test-Path $src) -and (-not (Test-Path "$winStage\$dll"))) {
                Copy-Item $src $winStage
                Write-Host "    + $dll (auto-detected)" -ForegroundColor DarkGray
            }
        }
    }
    # Also pick up any DLLs the build placed next to the exe
    Get-ChildItem "$ROOT\build-win\*.dll" -ErrorAction SilentlyContinue | Copy-Item -Destination $winStage

    Zip-Dir $winStage $WIN_ZIP
    Remove-Item $winStage -Recurse -Force
}

# ── Linux ─────────────────────────────────────────────────────────────────────
if (-not $SkipLinux) {
    Banner "Building Linux (WSL: $WSL)..."

    Wsl-Run 'cmake --build ~/cold-start-nx/build-pc -- -j\$(nproc)'
    Wsl-Run '/bin/sh ~/cold-start-nx/build-pc/bundle.sh'
    
    # Remove .log files inside the dist folder (using WSL find)
    Wsl-Run 'find ~/cold-start-nx/build-pc/dist -name ''.log'' -delete'
    
    # Zip using PowerShell (no WSL quoting issues)
    $distPath = "Z:\cold-start-nx\build-pc\dist"
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
