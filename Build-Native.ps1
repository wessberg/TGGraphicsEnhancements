# SPDX-License-Identifier: MIT
# Builds the three native DLLs, runs the native tests and stages each DLL into its mod's folder under
# artifacts/Native: TGGraphicsBridge, TGFrameGeneration and TGBetterUpscaling.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$StreamlineRoot,
    [Parameter(Mandatory=$true)][string]$NgxIncludeDir,
    [string]$FidelityFXRoot='',
    [string]$XeSSRoot='',
    [string]$AntiLag2Root='',
    # NVIDIA DLSS SDK release (github.com/NVIDIA/DLSS): the NGX headers and static library for DLSS Ray Reconstruction.
    [string]$DlssRoot='',
    [string]$MinHookRoot='',
    [string]$Generator='Visual Studio 17 2022'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw 'Install CMake and Visual Studio C++ build tools with Windows SDK 10.0.22621 or newer.' }
$StreamlineRoot=(Resolve-Path $StreamlineRoot).Path
$NgxIncludeDir=(Resolve-Path $NgxIncludeDir).Path
# Defaults are the SDK folders next to this script ($PSScriptRoot is not set during parameter binding in Windows PowerShell).
if(-not $FidelityFXRoot) { $FidelityFXRoot=Join-Path $PSScriptRoot 'FidelityFX-SDK-2.3.0' }
if(-not $XeSSRoot) { $XeSSRoot=Join-Path $PSScriptRoot 'XeSS-SDK-3.0.2' }
if(-not $AntiLag2Root) { $AntiLag2Root=Join-Path $PSScriptRoot 'AntiLag2-SDK-2.0.0a' }
if(-not $DlssRoot) { $DlssRoot=Join-Path $PSScriptRoot 'DLSS-SDK-310.9.1' }
$DlssRoot=(Resolve-Path $DlssRoot).Path
$FidelityFXRoot=(Resolve-Path $FidelityFXRoot).Path
$XeSSRoot=(Resolve-Path $XeSSRoot).Path
$AntiLag2Root=(Resolve-Path $AntiLag2Root).Path
$build=Join-Path $PSScriptRoot 'build\native'
$generatorArgs=@('-G',$Generator)
if($Generator -like 'Visual Studio*') { $generatorArgs+=@('-A','x64') }
else { $generatorArgs+='-DCMAKE_BUILD_TYPE=Release' } # single-config generators ignore --config
$args=@('-S',$PSScriptRoot,'-B',$build)+$generatorArgs+(
    '-DTGFG_BUILD_NATIVE=ON','-DBUILD_TESTING=ON',"-DSTREAMLINE_ROOT=$StreamlineRoot","-DNGX_INCLUDE_DIR=$NgxIncludeDir",
    "-DFIDELITYFX_ROOT=$FidelityFXRoot","-DXESS_ROOT=$XeSSRoot","-DANTILAG2_ROOT=$AntiLag2Root","-DDLSS_ROOT=$DlssRoot")
if($MinHookRoot) { $args+="-DMINHOOK_ROOT=$((Resolve-Path $MinHookRoot).Path)" }
& cmake @args
if($LASTEXITCODE -ne 0) { throw 'Native CMake configure failed.' }
& cmake --build $build --config Release --parallel
if($LASTEXITCODE -ne 0) { throw 'Native compilation failed. No game files were modified.' }
$stage=Join-Path $PSScriptRoot 'artifacts\Native'
& cmake --install $build --config Release --prefix $stage --component TGNative
if($LASTEXITCODE -ne 0) { throw 'Native staging failed.' }
# After staging: the GPU smoke test loads the staged DLLs and runtimes (it skips without them).
& ctest --test-dir $build -C Release --output-on-failure
if($LASTEXITCODE -ne 0) { throw 'Native policy, HLSL or GPU smoke tests failed.' }
& (Join-Path $PSScriptRoot 'tools\Write-NativeManifest.ps1') -Stage $stage -Headers @{
    streamline_headers=$StreamlineRoot; ngx_headers=$NgxIncludeDir; fidelityfx_headers=$FidelityFXRoot
    xess_headers=$XeSSRoot; antilag2_headers=$AntiLag2Root }
Write-Host "Native files staged to $stage. Add the NVIDIA, AMD, Intel and Microsoft (Agility SDK) runtimes using Stage-Runtime.ps1. No game installation was touched."
