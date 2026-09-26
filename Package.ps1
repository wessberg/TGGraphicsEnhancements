# SPDX-License-Identifier: MIT
# Packages (and with -Deploy installs) existing builds of the mods in THIS repository: one zip per
# mod and runtime, each holding only its BepInEx payload (with its copy of TG Graphics Bridge), its
# manifest and the Vortex mod type.
[CmdletBinding()]
param(
    [ValidateSet('Mono','IL2CPP','Both')][string]$Runtime = 'Mono',
    [ValidateSet('All','TGFrameGeneration','TGBetterUpscaling','TGPathTracing')][string]$Mod = 'All',
    [switch]$Deploy
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'tools\Package.Common.ps1')
if ($Deploy) { Assert-TgGameClosed }
$version = Get-TgVersion
$mods = @(Resolve-TgModNames $Mod)
$nativeRoot = Join-Path $PSScriptRoot 'artifacts\Native'
$nativeManifestPath = Join-Path $nativeRoot 'build-manifest.json'
Assert-TgFile $nativeManifestPath
$nativeManifest = Get-Content -LiteralPath $nativeManifestPath -Raw | ConvertFrom-Json
Assert-TgFields $nativeManifest @('version','abi','native_sha256') 'Native build manifest'
if ($nativeManifest.version -ne $version -or $nativeManifest.abi -ne (Get-TgAbi)) {
    throw "Expected the v$version native build (ABI $(Get-TgAbi)) in artifacts/Native. Run Build-Native.ps1."
}
# Every file of every requested mod is validated before anything is packaged or installed.
$nativeFiles = @{}
foreach ($name in $mods) {
    $m = Get-TgMod $name
    $files = @()
    $components = Get-TgNativeComponents
    foreach ($native in $m.Natives) {
        $artifact = $components[$native.Component]
        $dll = Join-Path $nativeRoot $artifact
        Assert-TgAmd64 $dll
        if ($nativeManifest.native_sha256.($native.Component) -ne (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash) {
            throw "$artifact does not match artifacts/Native/build-manifest.json. Rebuild with Build-Native.ps1 instead of mixing outputs."
        }
        $files += @{Source=$dll; Relative=$native.Relative}
    }
    foreach ($group in $m.Signed) {
        foreach ($file in @($group.Files) + @($group.Optional) + @($group.Notices)) {
            $path = Join-Path $nativeRoot "$name\$($group.Folder)\$file"
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                if ($group.Optional -contains $file) { continue }
                throw "Missing $name runtime file: $path. Run Stage-Runtime.ps1 first."
            }
            if ($group.Notices -notcontains $file) { Assert-TgSignature $path $group.Signer }
            $files += @{Source=$path; Relative="BepInEx/$name/Native/$($group.Folder)/$file"}
        }
    }
    if ($m.Extension) {
        $extension = Join-Path $PSScriptRoot $m.Extension
        Assert-TgFile $extension
        $files += @{Source=$extension; Relative=(Get-TgExtensionPath $name)}
    }
    $nativeFiles[$name] = $files
}
$targets = @(if ($Runtime -eq 'Both') { 'Mono','IL2CPP' } else { $Runtime })
$builds = @()
foreach ($target in $targets) {
    $root = Join-Path $PSScriptRoot "artifacts/$target"
    $metadataPath = Join-Path $root 'build-manifest.json'
    Assert-TgFile $metadataPath
    $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
    Assert-TgFields $metadata @('version','runtime','framework','managed_sha256','game_dir') 'Managed build manifest'
    if ($metadata.version -ne $version -or $metadata.runtime -ne $target -or
        $metadata.framework -ne (Get-TgFramework $target)) { throw "Rebuild the managed assemblies for $target using Build.ps1." }
    if ($Deploy) { Assert-TgGame $metadata.game_dir $target }
    foreach ($name in $mods) {
        if (-not (Test-TgModRuntime $name $target)) { Write-Host "$name has no $target version; skipped."; continue }
        $files = @()
        foreach ($entry in (Get-TgMod $name).Managed) {
            $path = Join-Path $root $entry.Relative
            Assert-TgManagedVersion $path $entry.Assembly "$version.0"
            if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $metadata.managed_sha256.($entry.Relative)) {
                throw "$($entry.Relative) ($target) does not match its build manifest. Rebuild instead of mixing outputs."
            }
            $files += @{Source=$path; Relative=$entry.Relative}
        }
        $builds += @{Target=$target; Mod=$name; Game=$metadata.game_dir; Files=($files + $nativeFiles[$name])}
    }
}
if ($Deploy -and $targets.Count -gt 1) {
    $destinations = @($targets | ForEach-Object { $t = $_; [IO.Path]::GetFullPath(($builds | Where-Object { $_.Target -eq $t } | Select-Object -First 1).Game).TrimEnd('\','/') } | Sort-Object -Unique)
    if ($destinations.Count -ne $targets.Count) { throw 'Mono and IL2CPP deployment targets must be different game installations.' }
}
$packages = @()
foreach ($build in $builds) {
    $target = $build.Target; $name = $build.Mod; $m = Get-TgMod $name
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $stage = Join-Path $PSScriptRoot "artifacts/Packages/$name-$version-$target-$stamp"
    $records = @()
    foreach ($entry in $build.Files) {
        $destination = Get-TgOwnedPath $name $stage $entry.Relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $entry.Source -Destination $destination -Force
        $records += [ordered]@{path=$entry.Relative; sha256=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash}
    }
    $manifestName = Get-TgPackageManifestName $name
    [ordered]@{
        mod=$name; title=$m.Title; version=$version; runtime=$target; framework=(Get-TgFramework $target)
        abi=(Get-TgAbi); bridge=$version
        utc=[DateTime]::UtcNow.ToString('o'); requires_launcher=$false; rendering_validated=$false
        files=$records
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $stage $manifestName) -Encoding UTF8
    # Vortex would otherwise install the payload under its own mod folder rather than the game root.
    Write-TgVortexOverrides $stage
    # Packages contain the BepInEx payload, its manifest and the Vortex mod type: no build tools,
    # launchers or instructions.
    New-TgZip $stage @('BepInEx',$manifestName,(Get-TgVortexOverrideName)) ($stage + '.zip')
    $packages += @{Stage=$stage; Game=$build.Game; Target=$target; Mod=$name}
    Write-Host "Packaged: $stage.zip"
    # The same package under the name it gets on Nexus Mods, replaced by every packaging run.
    $nexus = Join-Path $PSScriptRoot ('artifacts/Nexus/' + (Get-TgNexusName $name $target))
    New-Item -ItemType Directory -Path (Split-Path -Parent $nexus) -Force | Out-Null
    Copy-Item -LiteralPath ($stage + '.zip') -Destination $nexus -Force
    Write-Host "Nexus download: $nexus"
}
if ($Deploy) {
    foreach ($name in (Get-TgModNames)) {
        foreach ($package in @($packages | Where-Object { $_.Mod -eq $name })) { Install-TgPayload $package.Mod $package.Stage $package.Game $package.Target -Together $mods }
    }
    Write-Host 'Press Play in Steam. No launcher, launch arguments, or app-ID workaround is required by these packages.'
}
Write-Host 'Review the NVIDIA, AMD and Intel runtime distribution terms before publishing these runtime-containing packages.'
Write-Host 'No game EXE, graphics proxy, Doorstop configuration, Steam setting, or existing mod configuration was changed.'
