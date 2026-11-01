<#
  Downloads the official libopus release from Xiph, checks it against the SHA-256 Xiph publishes, and builds it as a
  static library (static CRT, x64) into build\opus for LipSyncTogether.dll's voice chat. Run it from a Visual Studio
  x64 developer prompt (build.bat calls it when build\opus is missing). libopus is BSD-licensed; its COPYING file is
  copied next to the library and shipped in the mod's Docs.
#>
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$version = "1.5.2"
# From https://downloads.xiph.org/releases/opus/SHA256SUMS.txt
$sha256 = "65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1"
$out = Join-Path $PSScriptRoot "build\opus"
if (Test-Path (Join-Path $out "lib\opus.lib")) { Write-Host "libopus $version already built in $out"; return }

$work = Join-Path $PSScriptRoot "build\opus-src"
New-Item -ItemType Directory -Force $work | Out-Null
$archive = Join-Path $work "opus-$version.tar.gz"
if (-not (Test-Path $archive)) {
    Write-Host "Downloading libopus $version..."
    Invoke-WebRequest "https://downloads.xiph.org/releases/opus/opus-$version.tar.gz" -OutFile $archive
}
$actual = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $sha256) { Remove-Item $archive; throw "opus-$version.tar.gz has SHA-256 $actual, expected $sha256" }

tar -xzf $archive -C $work
if ($LASTEXITCODE -ne 0) { throw "could not unpack $archive" }
$source = Join-Path $work "opus-$version"
$cmakeBuild = Join-Path $work "cmake"

# Visual Studio ships CMake; the developer prompt does not always put it on PATH.
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake -and $env:VSINSTALLDIR) {
    $cmake = Join-Path $env:VSINSTALLDIR "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}
if (-not $cmake -or -not (Test-Path $cmake)) { throw "CMake not found; install it or run from a Visual Studio developer prompt" }
function cmake { & $cmake @args }

cmake -S $source -B $cmakeBuild -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release `
    -DOPUS_STATIC_RUNTIME=ON `
    -DBUILD_SHARED_LIBS=OFF -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF `
    -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=OFF "-DCMAKE_INSTALL_PREFIX=$out"
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
cmake --build $cmakeBuild --target install
if ($LASTEXITCODE -ne 0) { throw "libopus build failed" }
Copy-Item (Join-Path $source "COPYING") (Join-Path $out "COPYING")
Write-Host "libopus $version built into $out"
