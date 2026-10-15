param(
    [string]$QtPrefix,
    [string]$CompilerBin,
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if ($repoRoot -match '[^\x00-\x7F]') {
    # MinGW's linker cannot resolve its runtime libraries through a non-ASCII path.
    # A junction keeps all source/tool files in the original repository.
    $asciiRoot = Join-Path $env:LOCALAPPDATA 'AShareBoardSource'
    if (Test-Path -LiteralPath $asciiRoot) {
        $existing = Get-Item -LiteralPath $asciiRoot
        if ($existing.LinkType -ne 'Junction' -or $existing.Target -ne $repoRoot) {
            throw "Existing path does not point to this repository: $asciiRoot"
        }
    } else {
        New-Item -ItemType Junction -Path $asciiRoot -Target $repoRoot | Out-Null
    }
    $repoRoot = $asciiRoot
}
if (-not $QtPrefix) { $QtPrefix = Join-Path $repoRoot '.tools/Qt/6.8.3/mingw_64' }
$portableCompiler = Join-Path $env:LOCALAPPDATA 'AShareBoardToolchain/mingw1310_64/bin'
if (-not $CompilerBin) {
    $CompilerBin = if (Test-Path (Join-Path $portableCompiler 'g++.exe')) { $portableCompiler }
                   else { Join-Path $repoRoot '.tools/Qt/Tools/mingw1310_64/bin' }
}
$localTools = Join-Path $repoRoot '.tools/venv/Scripts'
$QtPrefix = $QtPrefix.Replace('\', '/')
$CompilerBin = $CompilerBin.Replace('\', '/')
$env:PATH = "$CompilerBin;$QtPrefix/bin;$localTools;$env:PATH"
if (-not (Test-Path "$QtPrefix/lib/cmake/Qt6/Qt6Config.cmake")) { throw "Qt SDK not found: $QtPrefix" }
$buildDir = Join-Path $repoRoot 'ashare-build/native'
& cmake --fresh -S (Join-Path $repoRoot 'fincept-qt/ashare') -B $buildDir -G Ninja "-DCMAKE_PREFIX_PATH=$QtPrefix" "-DCMAKE_CXX_COMPILER=$CompilerBin/g++.exe" '-DCMAKE_CXX_FLAGS=-no-canonical-prefixes' -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& cmake --build $buildDir --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& ctest --test-dir $buildDir --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
if ($Package) {
    $distDir = Join-Path $repoRoot 'ashare-dist'
    New-Item -ItemType Directory -Force -Path $distDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $buildDir 'AShareBoard.exe') -Destination $distDir
    & "$QtPrefix/bin/windeployqt.exe" --release --compiler-runtime --no-translations --dir $distDir (Join-Path $distDir 'AShareBoard.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
    Copy-Item -LiteralPath (Join-Path $QtPrefix 'plugins/platforms/qoffscreen.dll') -Destination (Join-Path $distDir 'platforms')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $distDir
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination $distDir
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'THIRD_PARTY.md') -Destination $distDir
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'third-party') -Destination $distDir -Recurse -Force
    Write-Output "Ready: $distDir/AShareBoard.exe"
}
