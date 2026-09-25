# SPDX-License-Identifier: MIT
# Stages the signed vendor runtimes, with the license notices their terms require, into each mod's
# folder under artifacts/Native. Nothing is copied into the game folder.
#   TGFrameGeneration/Streamline   NVIDIA Streamline, DLSS Frame Generation, Reflex (interposer renamed)
#   TGFrameGeneration/FidelityFX   AMD FSR API loader and frame generation
#   TGFrameGeneration/XeSS         Intel XeSS Frame Generation and XeLL
#   TGBetterUpscaling/FidelityFX   AMD FSR API loader and upscaler (FSR 4 and FSR 3.1)
#   TGBetterUpscaling/XeSS         Intel XeSS Super Resolution
#   TGBetterUpscaling/DLSS         NVIDIA DLSS Super Resolution, for the game's own DLSS
[CmdletBinding()]
param(
    # NVIDIA Streamline production runtime folder (bin/x64 of the Streamline SDK release).
    [string]$RuntimeDirectory='',
    # AMD FSR SDK: bin/amd_fidelityfx_loader_dx12.dll, bin/amd_fidelityfx_framegeneration_dx12.dll,
    # bin/amd_fidelityfx_upscaler_dx12.dll, Kits/FidelityFX/docs/license.md.
    [string]$FidelityFXRoot='',
    # Intel XeSS SDK: bin/libxess.dll, bin/libxess_fg.dll, bin/libxell.dll, LICENSE.txt.
    [string]$XeSSRoot='',
    # NVIDIA DLSS SDK release (github.com/NVIDIA/DLSS): lib/Windows_x86_64/rel/nvngx_dlss.dll, LICENSE.txt.
    [string]$DlssRoot=''
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(-not ($RuntimeDirectory -or $FidelityFXRoot -or $XeSSRoot -or $DlssRoot)) {
    throw 'Pass -RuntimeDirectory (NVIDIA Streamline), -FidelityFXRoot (AMD FSR), -XeSSRoot (Intel XeSS) and/or -DlssRoot (NVIDIA DLSS).'
}
$native=Join-Path $PSScriptRoot 'artifacts\Native'
function Assert-Signed([string]$Path,[string]$Signer) {
    $signature=Get-AuthenticodeSignature $Path
    if($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate -or $signature.SignerCertificate.Subject -notmatch $Signer) {
        throw "Expected a valid $Signer production signature: $Path ($($signature.Status)). Nothing was staged."
    }
}
function Write-RuntimeManifest([string]$Folder,[object[]]$Entries) {
    $Entries | ConvertTo-Json | Set-Content (Join-Path $Folder 'runtime-manifest.json') -Encoding UTF8
}
# Every group is validated completely before any of its files are copied.
if($RuntimeDirectory) {
    $source=(Resolve-Path $RuntimeDirectory).Path
    $dest=Join-Path $native 'TGFrameGeneration\Streamline'
    $required=@('sl.interposer.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll','nvngx_dlssg.dll')
    $optional=@('nvapi64.dll')
    # The interposer is staged, byte-identical and still NVIDIA-signed, under a private name:
    # Streamline-aware injectors (RenoDX DLSS) find and hook a module named sl.interposer.dll.
    $renamed=@{'sl.interposer.dll'='TGFG.Streamline.dll'}
    # Use the official matching production package, not debug/development DLLs or files
    # collected from unrelated games.
    $files=@()
    foreach($name in $required+$optional) {
        $path=Join-Path $source $name
        if(-not (Test-Path $path)) {
            if($required -contains $name) { throw "Missing required runtime: $path" }
            continue
        }
        Assert-Signed $path 'NVIDIA'
        $files+=Get-Item $path
    }
    # NVIDIA's notices travel with the runtime: Streamline's MIT license (the SDK root's license.txt),
    # the NVIDIA RTX SDKs license covering DLSS Frame Generation, and the Reflex license.
    $notices=[ordered]@{
        'LICENSE-Streamline.txt'=(Join-Path $source '..\..\license.txt')
        'LICENSE-NVIDIA-RTX-SDKs.txt'=(Join-Path $source 'nvngx_dlss.license.txt')
        'LICENSE-NVIDIA-Reflex.txt'=(Join-Path $source 'reflex.license.txt')
    }
    foreach($notice in $notices.GetEnumerator()) {
        if(-not (Test-Path $notice.Value)) { throw "Missing NVIDIA license notice: $($notice.Value). Pass the SDK release's bin\x64 folder. Nothing was staged." }
    }
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    $manifest=@()
    foreach($file in $files) {
        $name=if($renamed.ContainsKey($file.Name)) { $renamed[$file.Name] } else { $file.Name }
        Copy-Item $file.FullName (Join-Path $dest $name) -Force
        $manifest+=[ordered]@{name=$name;source=$file.Name;fileVersion=$file.VersionInfo.FileVersion;sha256=(Get-FileHash $file.FullName -Algorithm SHA256).Hash}
    }
    foreach($original in $renamed.Keys) {
        $stale=Join-Path $dest $original
        if(Test-Path $stale) { Remove-Item $stale -Force } # staged by an earlier version under NVIDIA's name
    }
    foreach($notice in $notices.GetEnumerator()) { Copy-Item $notice.Value (Join-Path $dest $notice.Key) -Force }
    Write-RuntimeManifest $dest $manifest
    Write-Host "Staged signed NVIDIA runtime files and license notices to $dest."
}
# AMD and Intel ship signed binaries whose licenses require their notice to travel with them. Frame
# generation and upscaling each carry their own copy of what they load, so either mod works alone.
$vendors=@()
if($FidelityFXRoot) {
    $root=(Resolve-Path $FidelityFXRoot).Path
    $notice=@{Source=(Join-Path $root 'Kits\FidelityFX\docs\license.md'); Name='LICENSE-FidelityFX.md'}
    $vendors+=@{Folder='TGFrameGeneration\FidelityFX'; Signer='Advanced Micro Devices'; Notice=$notice;
        Files=@((Join-Path $root 'bin\amd_fidelityfx_loader_dx12.dll'),(Join-Path $root 'bin\amd_fidelityfx_framegeneration_dx12.dll'))}
    $vendors+=@{Folder='TGBetterUpscaling\FidelityFX'; Signer='Advanced Micro Devices'; Notice=$notice;
        Files=@((Join-Path $root 'bin\amd_fidelityfx_loader_dx12.dll'),(Join-Path $root 'bin\amd_fidelityfx_upscaler_dx12.dll'))}
}
if($XeSSRoot) {
    $root=(Resolve-Path $XeSSRoot).Path
    $notice=@{Source=(Join-Path $root 'LICENSE.txt'); Name='LICENSE-XeSS.txt'}
    $vendors+=@{Folder='TGFrameGeneration\XeSS'; Signer='Intel Corporation'; Notice=$notice;
        Files=@((Join-Path $root 'bin\libxess_fg.dll'),(Join-Path $root 'bin\libxell.dll'))}
    $vendors+=@{Folder='TGBetterUpscaling\XeSS'; Signer='Intel Corporation'; Notice=$notice;
        Files=@((Join-Path $root 'bin\libxess.dll'))}
}
# The DLSS Super Resolution DLL the game's own DLSS gets when its nvngx_dlss.dll is older, under the
# NVIDIA RTX SDKs license of the same DLSS SDK release.
if($DlssRoot) {
    $root=(Resolve-Path $DlssRoot).Path
    $vendors+=@{Folder='TGBetterUpscaling\DLSS'; Signer='NVIDIA'; Notice=@{Source=(Join-Path $root 'LICENSE.txt'); Name='LICENSE-NVIDIA-RTX-SDKs.txt'};
        Files=@((Join-Path $root 'lib\Windows_x86_64\rel\nvngx_dlss.dll'))}
}
foreach($vendor in $vendors) {
    foreach($path in $vendor.Files) {
        if(-not (Test-Path $path)) { throw "Missing $($vendor.Folder) runtime: $path. Nothing was staged for $($vendor.Folder)." }
        Assert-Signed $path $vendor.Signer
    }
    if(-not (Test-Path $vendor.Notice.Source)) { throw "Missing $($vendor.Folder) license notice: $($vendor.Notice.Source)" }
    $dest=Join-Path $native $vendor.Folder
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    $manifest=@()
    foreach($path in $vendor.Files) {
        $file=Get-Item $path
        Copy-Item $file.FullName (Join-Path $dest $file.Name) -Force
        $manifest+=[ordered]@{name=$file.Name;fileVersion=$file.VersionInfo.FileVersion;sha256=(Get-FileHash $file.FullName -Algorithm SHA256).Hash}
    }
    Copy-Item $vendor.Notice.Source (Join-Path $dest $vendor.Notice.Name) -Force
    Write-RuntimeManifest $dest $manifest
    Write-Host "Staged signed $($vendor.Folder) runtime files and license notice to $dest."
}
Write-Host 'Nothing was copied into the game folder.'
