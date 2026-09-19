# SPDX-License-Identifier: MIT
# Package/deploy existing builds in THIS repository. No external ProjectRoot.
[CmdletBinding()]
param(
    [ValidateSet('Mono','IL2CPP','Both')][string]$Runtime = 'Mono',
    [switch]$Deploy
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'tools\Package.Common.ps1')
if ($Deploy) { Assert-TgfgGameClosed }
$nativeRoot = Join-Path $PSScriptRoot 'artifacts\Native'
$native = Join-Path $nativeRoot 'TGFG.Native.dll'
Assert-TgfgAmd64 $native
$nativeManifestPath = Join-Path $nativeRoot 'build-manifest.json'
Assert-TgfgFile $nativeManifestPath
$nativeManifest = Get-Content -LiteralPath $nativeManifestPath -Raw | ConvertFrom-Json
Assert-TgfgFields $nativeManifest @('version','native_sha256') 'Native build manifest'
if ($nativeManifest.version -ne '0.5.0' -or
    $nativeManifest.native_sha256 -ne (Get-FileHash -LiteralPath $native -Algorithm SHA256).Hash) {
    throw 'Expected the existing v0.5.0 native backend and matching hash (ABI 0x00050000). Rebuild/stage with Build-Native.ps1 only if it is missing or mismatched.'
}
$nativeFiles = @(@{Source=$native; Relative='BepInEx/TGFrameGeneration/Native/TGFG.Native.dll'})
$required = @(Get-TgfgRuntimeNames)
foreach ($name in $required + @('nvapi64.dll')) {
    $path = Join-Path $nativeRoot "Streamline/$name"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        if ($required -contains $name) { throw "Missing NVIDIA runtime: $path. Run Stage-Runtime.ps1 first." }
        continue
    }
    Assert-TgfgNvidiaSignature $path
    $nativeFiles += @{Source=$path; Relative="BepInEx/TGFrameGeneration/Native/Streamline/$name"}
}
foreach ($name in (Get-TgfgNvidiaNotices)) {
    $path = Join-Path $nativeRoot "Streamline/$name"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing NVIDIA license notice: $path. Run Stage-Runtime.ps1 -RuntimeDirectory first." }
    $nativeFiles += @{Source=$path; Relative="BepInEx/TGFrameGeneration/Native/Streamline/$name"}
}
foreach ($vendor in (Get-TgfgVendorRuntimes)) {
    foreach ($name in @($vendor.Files) + @($vendor.Notice)) {
        $path = Join-Path $nativeRoot "$($vendor.Folder)/$name"
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing $($vendor.Folder) runtime file: $path. Run Stage-Runtime.ps1 -FidelityFXRoot/-XeSSRoot first."
        }
        if ($vendor.Files -contains $name) { Assert-TgfgSignature $path $vendor.Signer }
        $nativeFiles += @{Source=$path; Relative="BepInEx/TGFrameGeneration/Native/$($vendor.Folder)/$name"}
    }
}
$targets = if ($Runtime -eq 'Both') { @('Mono','IL2CPP') } else { @($Runtime) }
$builds = @()
# Validate all requested managed builds BEFORE writing any game files.
foreach ($target in $targets) {
    $root = Join-Path $PSScriptRoot "artifacts/$target"
    $metadataPath = Join-Path $root 'build-manifest.json'
    Assert-TgfgFile $metadataPath
    $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
    Assert-TgfgFields $metadata @('version','runtime','framework','managed_sha256','autoload_sha256','game_dir') 'Managed build manifest'
    if ($metadata.version -ne '0.5.0' -or $metadata.runtime -ne $target -or
        $metadata.framework -ne (Get-TgfgFramework $target)) { throw "Rebuild the integrated managed pair for $target using Build.ps1." }
    $plugin = Join-Path $root 'BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll'
    $loader = Join-Path $root 'BepInEx/patchers/TGFG.Autoload.dll'
    Assert-TgfgManagedVersion $plugin 'TGFrameGeneration' '0.5.0.0'
    Assert-TgfgManagedVersion $loader 'TGFG.Autoload' '0.5.0.0'
    if ((Get-FileHash -LiteralPath $plugin -Algorithm SHA256).Hash -ne $metadata.managed_sha256 -or
        (Get-FileHash -LiteralPath $loader -Algorithm SHA256).Hash -ne $metadata.autoload_sha256) {
        throw "The $target DLLs do not match their build manifest. Rebuild instead of mixing outputs."
    }
    if ($Deploy) { Assert-TgfgGame $metadata.game_dir $target }
    $builds += @{Target=$target; Metadata=$metadata; Files=(@(
        @{Source=$plugin; Relative='BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll'},
        @{Source=$loader; Relative='BepInEx/patchers/TGFG.Autoload.dll'}
    ) + $nativeFiles)}
}
if ($Deploy -and $builds.Count -gt 1) {
    $destinations = @($builds | ForEach-Object { [IO.Path]::GetFullPath($_.Metadata.game_dir).TrimEnd('\','/') } | Sort-Object -Unique)
    if ($destinations.Count -ne $builds.Count) { throw 'Mono and IL2CPP deployment targets must be different game installations.' }
}
$packages = @()
foreach ($build in $builds) {
    $target = $build.Target
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $stage = Join-Path $PSScriptRoot "artifacts/Packages/TGFrameGeneration-0.5.0-$target-$stamp"
    $records = @()
    foreach ($entry in $build.Files) {
        $destination = Get-TgfgOwnedPath $stage $entry.Relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $entry.Source -Destination $destination -Force
        $records += [ordered]@{path=$entry.Relative; sha256=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash}
    }
    [ordered]@{
        version='0.5.0'; runtime=$target; framework=(Get-TgfgFramework $target)
        native_version='0.5.0'; native_abi='0x00050000'; utc=[DateTime]::UtcNow.ToString('o')
        requires_launcher=$false; steam_startup_validated=$false; rendering_validated=$false
        files=$records
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $stage 'TGFG-Package-manifest.json') -Encoding UTF8
    # Packages contain the BepInEx payload and its manifest: no build tools, launchers or instructions.
    New-TgfgZip $stage @('BepInEx','TGFG-Package-manifest.json') ($stage + '.zip')
    $packages += @{Stage=$stage; Game=$build.Metadata.game_dir; Target=$target}
    Write-Host "Packaged: $stage.zip"
    # The same package under the name it gets on Nexus Mods, replaced by every packaging run.
    $nexus = Join-Path $PSScriptRoot ('artifacts/Nexus/' + (Get-TgfgNexusName $target))
    New-Item -ItemType Directory -Path (Split-Path -Parent $nexus) -Force | Out-Null
    Copy-Item -LiteralPath ($stage + '.zip') -Destination $nexus -Force
    Write-Host "Nexus download: $nexus"
}
if ($Deploy) {
    foreach ($package in $packages) { Install-TgfgPayload $package.Stage $package.Game $package.Target }
}
Write-Host 'Review the NVIDIA, AMD and Intel runtime distribution terms before publishing these runtime-containing packages.'
Write-Host 'No game EXE, graphics proxy, Doorstop configuration, Steam setting, or existing mod configuration was changed.'
