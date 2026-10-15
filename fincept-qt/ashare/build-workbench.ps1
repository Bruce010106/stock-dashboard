param([string]$QtPrefix, [string]$CompilerBin, [string]$OpenSSLRoot)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if ($repoRoot -match '[^\x00-\x7F]') {
    $asciiRoot = Join-Path $env:LOCALAPPDATA 'AShareBoardSource'
    if (Test-Path -LiteralPath $asciiRoot) {
        $existing = Get-Item -LiteralPath $asciiRoot
        if ($existing.LinkType -ne 'Junction' -or $existing.Target -ne $repoRoot) {
            throw "Existing junction belongs to another repository: $asciiRoot"
        }
    } else {
        New-Item -ItemType Junction -Path $asciiRoot -Target $repoRoot | Out-Null
    }
    $repoRoot = $asciiRoot
}
$repoRoot = $repoRoot.Replace('\', '/')
if (-not $QtPrefix) { $QtPrefix = "$repoRoot/.tools/Qt/6.8.3/mingw_64" }
if (-not $CompilerBin) { $CompilerBin = "$repoRoot/.tools/Qt/Tools/mingw1310_64/bin" }
if (-not $OpenSSLRoot) { $OpenSSLRoot = "$repoRoot/.tools/openssl/mingw64" }
$QtPrefix = $QtPrefix.Replace('\', '/')
$CompilerBin = $CompilerBin.Replace('\', '/')
$OpenSSLRoot = $OpenSSLRoot.Replace('\', '/')
$env:PATH = "$CompilerBin;$QtPrefix/bin;$repoRoot/.tools/venv/Scripts;$env:PATH"
# CMake resolves junctions to their physical path. Keep its actual installation
# outside the Chinese source path so generated Windows batch commands are ASCII.
$cmakeHome = Join-Path $env:LOCALAPPDATA 'AShareBoardToolchain/cmake-3.27.7'
if (-not (Test-Path -LiteralPath $cmakeHome)) {
    Copy-Item -LiteralPath "$repoRoot/.tools/venv/Lib/site-packages/cmake/data" -Destination $cmakeHome -Recurse
}
$cmakeExe = Join-Path $cmakeHome 'bin/cmake.exe'
$build = "$repoRoot/ashare-build/workbench"
& $cmakeExe -S "$repoRoot/fincept-qt" -B $build -G Ninja `
    "-DCMAKE_PREFIX_PATH=$QtPrefix" "-DCMAKE_C_COMPILER=$CompilerBin/gcc.exe" `
    "-DCMAKE_CXX_COMPILER=$CompilerBin/g++.exe" '-DCMAKE_C_FLAGS=-no-canonical-prefixes' `
    '-DCMAKE_CXX_FLAGS=-no-canonical-prefixes' -DCMAKE_BUILD_TYPE=Release `
    -DFINCEPT_DEV_BUILD=ON -DFINCEPT_ENABLE_LTO=OFF -DFINCEPT_MAX_COMPILE_JOBS=1 `
    -DFINCEPT_DOWNLOAD_YTDLP=OFF "-DOPENSSL_ROOT_DIR=$OpenSSLRoot"
if ($LASTEXITCODE -ne 0) { throw 'Workbench configure failed' }
& $cmakeExe --build $build --parallel 1
if ($LASTEXITCODE -ne 0) { throw 'Workbench build failed' }
Write-Output "Built full terminal: $build/FinceptTerminal.exe"
