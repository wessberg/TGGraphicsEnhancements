# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$StreamlineRoot,
    [Parameter(Mandatory=$true)][string]$NgxIncludeDir,
    [string]$MinHookRoot='',
    [string]$Generator='Visual Studio 17 2022'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw 'Install CMake and Visual Studio C++ build tools with Windows SDK 10.0.22621 or newer.' }
$StreamlineRoot=(Resolve-Path $StreamlineRoot).Path
$NgxIncludeDir=(Resolve-Path $NgxIncludeDir).Path
$build=Join-Path $PSScriptRoot 'build\native'
$generatorArgs=@('-G',$Generator)
if($Generator -like 'Visual Studio*') { $generatorArgs+=@('-A','x64') }
else { $generatorArgs+='-DCMAKE_BUILD_TYPE=Release' } # single-config generators ignore --config
$args=@('-S',$PSScriptRoot,'-B',$build)+$generatorArgs+(
    '-DTGFG_BUILD_NATIVE=ON','-DBUILD_TESTING=ON',"-DSTREAMLINE_ROOT=$StreamlineRoot","-DNGX_INCLUDE_DIR=$NgxIncludeDir")
if($MinHookRoot) { $args+="-DMINHOOK_ROOT=$((Resolve-Path $MinHookRoot).Path)" }
& cmake @args
if($LASTEXITCODE -ne 0) { throw 'Native CMake configure failed.' }
& cmake --build $build --config Release --parallel
if($LASTEXITCODE -ne 0) { throw 'Native compilation failed. No game files were modified.' }
& ctest --test-dir $build -C Release --output-on-failure
if($LASTEXITCODE -ne 0) { throw 'Native policy or Windows HLSL-compilation tests failed.' }
$stage=Join-Path $PSScriptRoot 'artifacts\Native'
& cmake --install $build --config Release --prefix $stage
if($LASTEXITCODE -ne 0) { throw 'Native staging failed.' }
[ordered]@{
    utc=[DateTime]::UtcNow.ToString('o')
    architecture='x64'
    version='0.4.0'
    backend='D3D11 rendering -> shared GPU resources -> private D3D12 NVIDIA Streamline presentation'
    streamline_headers=$StreamlineRoot
    ngx_headers=$NgxIncludeDir
    native_sha256=(Get-FileHash (Join-Path $stage 'TGFG.Native.dll') -Algorithm SHA256).Hash
    launcher_sha256=(Get-FileHash (Join-Path $stage 'TGFG.Launcher.exe') -Algorithm SHA256).Hash
    game_tested=$false
} | ConvertTo-Json | Set-Content (Join-Path $stage 'build-manifest.json') -Encoding UTF8
Write-Host "Native files staged to $stage. Add the NVIDIA runtimes using Stage-Runtime.ps1. No game installation was touched."
