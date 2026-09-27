# SPDX-License-Identifier: MIT
# Shared package checks for the mods built from this repository. Importing this file does not
# build, copy, or launch anything.
#   TGFrameGeneration  frame generation (DLSS, FSR, XeSS), a TG Graphics Bridge extension
#   TGBetterUpscaling  FSR and XeSS upscaling and a newer DLSS DLL, a TG Graphics Bridge extension
#   TGPathTracing      ReSTIR path tracing on the bridge's D3D12 device, a TG Graphics Bridge extension (Mono only)
# Each mod bundles TG Graphics Bridge (the DX11-to-DX12 bridge, its preloader and the shared gameplay
# library) under its own folders, so a player installs one download and two mods never deploy the
# same file; at run time one copy starts (see src/TGGraphicsBridge.Autoload/Bootstrap.cs).
$script:TgVersion = '1.2.0'
$script:TgAbi = '0x00010100'
function Get-TgVersion { $script:TgVersion }
function Get-TgAbi { $script:TgAbi }
function Get-TgModNames { @('TGFrameGeneration','TGBetterUpscaling','TGPathTracing') }
# The game versions a mod is built for: both unless its table says otherwise.
function Get-TgModRuntimes([string]$Name) {
    $m = Get-TgMod $Name
    if ($m.ContainsKey('Runtimes')) { return @($m.Runtimes) }
    @('Mono','IL2CPP')
}
function Test-TgModRuntime([string]$Name, [string]$Runtime) { @(Get-TgModRuntimes $Name) -contains $Runtime }
# The native DLLs Build-Native.ps1 stages under artifacts/Native, by component.
function Get-TgNativeComponents {
    [ordered]@{ TGGraphicsBridge='TGGraphicsBridge/TGGraphicsBridge.Native.dll'
                TGFrameGeneration='TGFrameGeneration/TGFG.Native.dll'; TGBetterUpscaling='TGBetterUpscaling/TGBetterUpscaling.Native.dll'
                TGPathTracing='TGPathTracing/TGPathTracing.Native.dll' }
}
# TG Graphics Bridge as a mod bundles it: its gameplay plugin beside the mod's own (the same GUID in
# every mod, BepInEx loads the newest), the mod's own copy of its preloader, and its native DLL.
function Get-TgBridgeManaged([string]$Mod) {
    @(@{Relative="BepInEx/plugins/$Mod/TGGraphicsBridge.dll"; Assembly='TGGraphicsBridge'},
      @{Relative="BepInEx/patchers/$Mod.Autoload.dll"; Assembly="$Mod.Autoload"})
}
function Get-TgBridgeNative([string]$Mod) { @{Component='TGGraphicsBridge'; Relative="BepInEx/$Mod/Native/TGGraphicsBridge.Native.dll"} }
# A 1.1.0 test build installed the bridge as a mod of its own; either mod's deployment removes it.
function Get-TgStandaloneBridge {
    @('BepInEx/patchers/TGGraphicsBridge.Autoload.dll','BepInEx/plugins/TGGraphicsBridge/TGGraphicsBridge.dll',
      'BepInEx/TGGraphicsBridge/Native/TGGraphicsBridge.Native.dll','BepInEx/TGGraphicsBridge/Install-manifest.json')
}
# One mod's payload. Managed: the assemblies Build.ps1 stages. Natives: the DLLs Build-Native.ps1
# stages under artifacts/Native. Signed: vendor runtimes (Stage-Runtime.ps1) with the notices their
# terms require alongside. Extension: the manifest by which the bridge's preloader finds the mod and
# the copy of the bridge it bundles.
function Get-TgMod([string]$Name) {
    switch ($Name) {
        'TGFrameGeneration' {
            @{Name=$Name; Title='TG Frame Generation'
              Managed=@(@{Relative='BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll'; Assembly='TGFrameGeneration'}) + (Get-TgBridgeManaged $Name)
              Natives=@(@{Component=$Name; Relative='BepInEx/TGFrameGeneration/Native/TGFG.Native.dll'}, (Get-TgBridgeNative $Name))
              # TGFG.Streamline.dll is NVIDIA's sl.interposer.dll under a private name (see Stage-Runtime.ps1).
              Signed=@(
                @{Folder='Streamline'; Signer='NVIDIA'; Optional=@('nvapi64.dll')
                  Files=@('TGFG.Streamline.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll','nvngx_dlssg.dll')
                  Notices=@('LICENSE-Streamline.txt','LICENSE-NVIDIA-RTX-SDKs.txt','LICENSE-NVIDIA-Reflex.txt')},
                @{Folder='FidelityFX'; Signer='Advanced Micro Devices'; Optional=@()
                  Files=@('amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_framegeneration_dx12.dll'); Notices=@('LICENSE-FidelityFX.md')},
                @{Folder='XeSS'; Signer='Intel Corporation'; Optional=@()
                  Files=@('libxess_fg.dll','libxell.dll'); Notices=@('LICENSE-XeSS.txt')})
              Extension='src/TGFrameGeneration/graphics-bridge-extension.json'
              # Installed by earlier versions: 1.0.x's own preloader, and the interposer under NVIDIA's name.
              Obsolete=@('BepInEx/patchers/TGFG.Autoload.dll','BepInEx/TGFrameGeneration/Native/Streamline/sl.interposer.dll') + (Get-TgStandaloneBridge)}
        }
        'TGBetterUpscaling' {
            @{Name=$Name; Title='TG Better Upscaling'
              Managed=@(@{Relative='BepInEx/plugins/TGBetterUpscaling/TGBetterUpscaling.dll'; Assembly='TGBetterUpscaling'}) + (Get-TgBridgeManaged $Name)
              Natives=@(@{Component=$Name; Relative='BepInEx/TGBetterUpscaling/Native/TGBetterUpscaling.Native.dll'}, (Get-TgBridgeNative $Name))
              Signed=@(
                @{Folder='FidelityFX'; Signer='Advanced Micro Devices'; Optional=@()
                  Files=@('amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_upscaler_dx12.dll'); Notices=@('LICENSE-FidelityFX.md')},
                @{Folder='XeSS'; Signer='Intel Corporation'; Optional=@()
                  Files=@('libxess.dll'); Notices=@('LICENSE-XeSS.txt')},
                # For the game's own DLSS, when the nvngx_dlss.dll it loads is older (native/upscaling/src/dlss_dll.cpp),
                # and DLSS Ray Reconstruction for TG Path Tracing's frames (native/upscaling/src/rr_upscaler.cpp).
                @{Folder='DLSS'; Signer='NVIDIA'; Optional=@()
                  Files=@('nvngx_dlss.dll','nvngx_dlssd.dll'); Notices=@('LICENSE-NVIDIA-RTX-SDKs.txt')})
              Extension='src/TGBetterUpscaling/graphics-bridge-extension.json'
              Obsolete=@(Get-TgStandaloneBridge)}
        }
        'TGPathTracing' {
            # The tracer is this repository's code on the bridge's D3D12 device; Microsoft's Agility SDK runtime
            # comes along for Shader Model 6.9 (DirectX Raytracing 1.2), which the bridge points the game at
            # where Windows' own runtime is older (native/bridge/src/agility.cpp), and AMD's FSR Ray Regeneration
            # denoises on a Radeon RX 9000 (native/pathtracing/src/ray_regeneration.cpp). The Mono version only:
            # its scene capture reads the game's ECS world through Unity.Entities' generic API.
            @{Name=$Name; Title='TG Path Tracing'; Runtimes=@('Mono')
              Managed=@(@{Relative='BepInEx/plugins/TGPathTracing/TGPathTracing.dll'; Assembly='TGPathTracing'}) + (Get-TgBridgeManaged $Name)
              Natives=@(@{Component=$Name; Relative='BepInEx/TGPathTracing/Native/TGPathTracing.Native.dll'}, (Get-TgBridgeNative $Name))
              Signed=@(
                @{Folder='D3D12'; Signer='Microsoft Corporation'; Optional=@()
                  Files=@('D3D12Core.dll'); Notices=@('LICENSE-DirectX-Agility-SDK.txt')},
                @{Folder='FidelityFX'; Signer='Advanced Micro Devices'; Optional=@()
                  Files=@('amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_denoiser_dx12.dll'); Notices=@('LICENSE-FidelityFX.md')},
                # The notices of what TGPathTracing.Native.dll compiles in (CMakeLists.txt installs them next to it): NVIDIA's
                # RTXDI and NRD (NVIDIA RTX SDKs license) and NRD's MathLib and ShaderMake reader (MIT). Packages are made from
                # builds with NRD; a build without it has no MathLib or ShaderMake notice, and packaging says so.
                @{Folder='Notices'; Signer=$null; Optional=@(); Files=@()
                  Notices=@('LICENSE-NVIDIA-RTX-SDKs.txt','LICENSE-MathLib.txt','LICENSE-ShaderMake.txt')})
              Extension='src/TGPathTracing/graphics-bridge-extension.json'
              Obsolete=@(Get-TgStandaloneBridge)}
        }
        default { throw "Unknown mod: $Name" }
    }
}
function Resolve-TgModNames([string]$Mod) {
    if ($Mod -eq 'All') { return @(Get-TgModNames) }
    if (@(Get-TgModNames) -notcontains $Mod) { throw "Unknown mod: $Mod" }
    @($Mod)
}
function Get-TgExtensionPath([string]$Mod) { "BepInEx/$Mod/graphics-bridge-extension.json" }
function Get-TgPackageManifestName([string]$Mod) { "$Mod-Package-manifest.json" }
function Get-TgInstalledManifestPath([string]$Mod) { "BepInEx/$Mod/Install-manifest.json" }
function Assert-TgFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing file: $Path" }
}
function Assert-TgGameClosed {
    if (Get-Process -Name 'Fall of Avalon' -ErrorAction SilentlyContinue) {
        throw 'Close FoA before deployment. Start normally through Steam afterward.'
    }
}
function Get-TgFramework([string]$Runtime) {
    switch ($Runtime) { 'Mono' { 'net472' }; 'IL2CPP' { 'net6.0' }; default { throw "Unknown runtime: $Runtime" } }
}
# The Nexus Mods download for each mod and game version: the package under a stable name.
function Get-TgNexusName([string]$Mod, [string]$Runtime) {
    [void](Get-TgMod $Mod)
    switch ($Runtime) { 'Mono' { "$Mod - mono.zip" }; 'IL2CPP' { "$Mod.zip" }; default { throw "Unknown runtime: $Runtime" } }
}
# Vortex picks a mod type to decide where it deploys an archive, and guesses wrong for a BepInEx
# payload: it lands under the game's own mod folder instead of the game root. Vortex 1.14 and later
# read this file from the archive root, and 'dinput' - shown as "Engine Injector" - is the built-in
# mod type that always deploys to the game root. Only the destination changes; the Vortex game
# extension still decides which files to copy, and Vortex ignores the type if the game rejects it.
function Get-TgVortexOverrideName { 'vortex_override_instructions.json' }
function Write-TgVortexOverrides([string]$Stage) {
    $json = ConvertTo-Json -InputObject @([ordered]@{type='setmodtype'; value='dinput'}) -Depth 3
    # Vortex reads this with Node, whose JSON.parse rejects a UTF-8 BOM.
    [IO.File]::WriteAllText((Join-Path $Stage (Get-TgVortexOverrideName)), $json,
        (New-Object Text.UTF8Encoding $false))
}
# Zip entries use forward slashes, as the zip format requires. Compress-Archive in Windows PowerShell
# 5.1 writes backslashes, which some extractors and archive previews show as flat file names.
function New-TgZip([string]$Stage, [string[]]$Items, [string]$Destination) {
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Force }
    $root = [IO.Path]::GetFullPath($Stage).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $archive = [System.IO.Compression.ZipFile]::Open($Destination, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($item in $Items) {
            $path = Join-Path $Stage $item
            $files = if (Test-Path -LiteralPath $path -PathType Container) { @(Get-ChildItem -LiteralPath $path -Recurse -File | Sort-Object FullName) } else { @(Get-Item -LiteralPath $path) }
            foreach ($file in $files) {
                $name = $file.FullName.Substring($root.Length).Replace('\','/')
                [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $name, [System.IO.Compression.CompressionLevel]::Optimal)
            }
        }
    } finally { $archive.Dispose() }
}
function Get-TgRequiredPaths([string]$Mod) {
    $m = Get-TgMod $Mod
    foreach ($managed in $m.Managed) { $managed.Relative }
    foreach ($native in $m.Natives) { $native.Relative }
    foreach ($group in $m.Signed) {
        foreach ($name in @($group.Files) + @($group.Notices)) { "BepInEx/$Mod/Native/$($group.Folder)/$name" }
    }
    if ($m.Extension) { Get-TgExtensionPath $Mod }
}
function Get-TgAllowedPaths([string]$Mod) {
    Get-TgRequiredPaths $Mod
    foreach ($group in (Get-TgMod $Mod).Signed) {
        foreach ($name in $group.Optional) { "BepInEx/$Mod/Native/$($group.Folder)/$name" }
    }
}
function Get-TgOwnedPath([string]$Mod, [string]$Root, [string]$Relative) {
    $relative = $Relative.Replace('\','/')
    if (@(Get-TgAllowedPaths $Mod) -notcontains $relative) { throw "Not a $Mod package payload path: $Relative" }
    Join-Path $Root $relative
}
function Assert-TgFields($Object, [string[]]$Fields, [string]$Label) {
    if ($null -eq $Object) { throw "Empty manifest: $Label" }
    foreach ($field in $Fields) {
        if ($Object.PSObject.Properties.Name -notcontains $field) { throw "$Label is missing field: $field" }
    }
}
function Assert-TgManagedVersion([string]$Path, [string]$Name, [string]$Version) {
    Assert-TgFile $Path
    # Metadata only: no assembly is loaded into the PowerShell execution context.
    $identity = [Reflection.AssemblyName]::GetAssemblyName($Path)
    if ($identity.Name -ne $Name -or $identity.Version.ToString() -ne $Version) {
        throw "Expected $Name $Version, found $identity at $Path. Rebuild the selected runtime."
    }
}
function Assert-TgAmd64([string]$Path) {
    Assert-TgFile $Path
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D) { throw "Invalid PE file: $Path" }
        $stream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt ($stream.Length - 6)) { throw "Invalid PE header: $Path" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw "Expected AMD64 image: $Path" }
    } finally { $reader.Dispose(); $stream.Dispose() }
}
function Assert-TgSignature([string]$Path, [string]$Signer) {
    Assert-TgAmd64 $Path
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch $Signer) {
        throw "Expected a valid $Signer production signature: $Path ($($signature.Status))"
    }
}
function Assert-TgGame([string]$GameDir, [string]$Runtime) {
    Assert-TgFile (Join-Path $GameDir 'Fall of Avalon.exe')
    Assert-TgFile (Join-Path $GameDir 'UnityPlayer.dll')
    $core = if ($Runtime -eq 'Mono') { 'BepInEx/core/BepInEx.dll' } else { 'BepInEx/core/BepInEx.Unity.IL2CPP.dll' }
    Assert-TgFile (Join-Path $GameDir $core)
    # Do not delete alternate copies; they may be local development files. The bridge's plugin is
    # expected once per mod that bundles it.
    $expected = @{}
    foreach ($mod in (Get-TgModNames)) {
        foreach ($managed in (Get-TgMod $mod).Managed) {
            $name = Split-Path -Leaf $managed.Relative
            if (-not $expected.ContainsKey($name)) { $expected[$name] = @() }
            $expected[$name] += [IO.Path]::GetFullPath((Join-Path $GameDir $managed.Relative))
        }
    }
    foreach ($folder in @('BepInEx/patchers','BepInEx/plugins')) {
        $scan = Join-Path $GameDir $folder
        if (-not (Test-Path -LiteralPath $scan)) { continue }
        $obsolete = @(Get-TgStandaloneBridge | ForEach-Object { [IO.Path]::GetFullPath((Join-Path $GameDir $_)) })
        foreach ($name in $expected.Keys) {
            foreach ($file in @(Get-ChildItem -LiteralPath $scan -Recurse -File -Filter $name)) {
                $full = [IO.Path]::GetFullPath($file.FullName)
                if ($obsolete -contains $full) { continue }
                if ($expected[$name] -notcontains $full) {
                    throw "Duplicate/misplaced assembly: $($file.FullName). Move it outside patchers/plugins first."
                }
            }
        }
    }
}
# -Installed: an installed mod, checked against its own manifest (a player may have one mod newer than
# the other); otherwise a package of this repository's build.
function Test-TgPayload([string]$Mod, $Manifest, [string]$Root, [string]$Runtime, [switch]$Installed) {
    Assert-TgFields $Manifest @('mod','version','runtime','framework','abi','files') "$Mod package manifest"
    $version = if ($Installed) { [string]$Manifest.version } else { Get-TgVersion }
    if ($Manifest.mod -ne $Mod -or $Manifest.runtime -ne $Runtime -or $Manifest.framework -ne (Get-TgFramework $Runtime) -or
        (-not $Installed -and ($Manifest.version -ne $version -or $Manifest.abi -ne (Get-TgAbi))) -or
        ($Installed -and $version -notmatch '^\d+\.\d+\.\d+$')) {
        throw "$Mod package metadata does not match the selected $Runtime v$(Get-TgVersion) / ABI $(Get-TgAbi) build."
    }
    $m = Get-TgMod $Mod
    $seen = @{}
    foreach ($entry in @($Manifest.files)) {
        Assert-TgFields $entry @('path','sha256') "$Mod package file entry"
        $relative = ([string]$entry.path).Replace('\','/')
        if ($seen.ContainsKey($relative)) { throw "Duplicate payload entry: $relative" }
        $seen[$relative] = $true
        $path = Get-TgOwnedPath $Mod $Root $relative
        Assert-TgFile $path
        if ([string]$entry.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) {
            throw "Payload differs from its manifest: $path"
        }
    }
    foreach ($required in (Get-TgRequiredPaths $Mod)) {
        if (-not $seen.ContainsKey($required)) { throw "Missing required $Mod payload entry: $required" }
    }
    foreach ($managed in $m.Managed) {
        # The bridge's plugin carries the bridge's version, which a mod of another version may bundle.
        if ($Installed -and $managed.Assembly -eq 'TGGraphicsBridge') { Assert-TgFile (Join-Path $Root $managed.Relative); continue }
        Assert-TgManagedVersion (Join-Path $Root $managed.Relative) $managed.Assembly "$version.0"
    }
    foreach ($native in $m.Natives) { Assert-TgAmd64 (Join-Path $Root $native.Relative) }
    foreach ($group in $m.Signed) {
        foreach ($name in @($group.Files) + @($group.Optional)) {
            $relative = "BepInEx/$Mod/Native/$($group.Folder)/$name"
            if ($seen.ContainsKey($relative)) { Assert-TgSignature (Get-TgOwnedPath $Mod $Root $relative) $group.Signer }
        }
    }
}
# Copies over a destination by replacing it: a file Vortex deployed is a hard link or symbolic link
# into its staging folder, and writing through it would change Vortex's copy as well.
function Copy-TgReplacing([string]$Source, [string]$Dest) {
    if (Test-Path -LiteralPath $Dest) { Remove-Item -LiteralPath $Dest -Force }
    New-Item -ItemType Directory -Path (Split-Path -Parent $Dest) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Dest -Force
}
# -Together: the mods deployed in the same run, whose bridges come from this same build.
function Install-TgPayload([string]$Mod, [string]$Stage, [string]$GameDir, [string]$Runtime, [string[]]$Together = @()) {
    Assert-TgGameClosed
    Assert-TgGame $GameDir $Runtime
    $manifestPath = Join-Path $Stage (Get-TgPackageManifestName $Mod)
    Assert-TgFile $manifestPath
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Test-TgPayload $Mod $manifest $Stage $Runtime
    # Copies of one bridge version must be the same files, or the one that wins at run time may be the
    # stale one. A different version is fine: the newest starts.
    foreach ($other in @(Get-TgModNames | Where-Object { $_ -ne $Mod -and $Together -notcontains $_ })) {
        $otherPath = Join-Path $GameDir (Get-TgInstalledManifestPath $other)
        if (-not (Test-Path -LiteralPath $otherPath -PathType Leaf)) { continue }
        $installed = Get-Content -LiteralPath $otherPath -Raw | ConvertFrom-Json
        if (@($installed.PSObject.Properties.Name) -notcontains 'bridge' -or $installed.bridge -ne $manifest.bridge) { continue }
        foreach ($pair in @(@((Get-TgBridgeManaged $Mod)[0].Relative, (Get-TgBridgeManaged $other)[0].Relative),
                            @((Get-TgBridgeNative $Mod).Relative, (Get-TgBridgeNative $other).Relative))) {
            $theirs = Join-Path $GameDir $pair[1]
            if ((Test-Path -LiteralPath $theirs -PathType Leaf) -and
                (Get-FileHash -LiteralPath (Join-Path $Stage $pair[0]) -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $theirs -Algorithm SHA256).Hash) {
                throw "$other is installed with a different build of TG Graphics Bridge $($manifest.bridge). Deploy both mods from the same build (-Mod All), or raise the bridge version."
            }
        }
    }
    $packaged = @($manifest.files | ForEach-Object { $_.path.Replace('\','/') })
    foreach ($group in (Get-TgMod $Mod).Signed) {
        foreach ($name in $group.Optional) {
            $optional = "BepInEx/$Mod/Native/$($group.Folder)/$name"
            if ((Test-Path -LiteralPath (Join-Path $GameDir $optional)) -and $packaged -notcontains $optional) {
                throw "Installed $Mod runtime has $name but the new package does not. Stage the intended matching runtime set or move that optional DLL aside first."
            }
        }
    }
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $backup = Join-Path $GameDir "BepInEx/$Mod/Backups/$stamp-$Runtime"
    $plan = @()
    foreach ($relative in $packaged) {
        $plan += @{Source=(Get-TgOwnedPath $Mod $Stage $relative); Relative=$relative;
                   Dest=(Get-TgOwnedPath $Mod $GameDir $relative); Saved=(Join-Path $backup $relative)}
    }
    foreach ($relative in (Get-TgMod $Mod).Obsolete) {
        $dest = Join-Path $GameDir $relative
        if (Test-Path -LiteralPath $dest -PathType Leaf) {
            $plan += @{Source=$null; Relative=$relative; Dest=$dest; Saved=(Join-Path $backup $relative); Remove=$true}
        }
    }
    # Installed metadata is written last, so a partial copy never certifies a complete update.
    $relative = Get-TgInstalledManifestPath $Mod
    $plan += @{Source=$manifestPath; Relative=$relative; Dest=(Join-Path $GameDir $relative); Saved=(Join-Path $backup $relative)}
    foreach ($entry in $plan) {
        $entry.Existed = Test-Path -LiteralPath $entry.Dest -PathType Leaf
        if ($entry.Existed) {
            New-Item -ItemType Directory -Path (Split-Path -Parent $entry.Saved) -Force | Out-Null
            Copy-Item -LiteralPath $entry.Dest -Destination $entry.Saved -Force
        }
    }
    $attempted = New-Object 'System.Collections.Generic.List[object]'
    try {
        foreach ($entry in $plan) {
            $attempted.Add($entry)
            if ($entry.ContainsKey('Remove')) { Remove-Item -LiteralPath $entry.Dest -Force; continue }
            Copy-TgReplacing $entry.Source $entry.Dest
            if ((Get-FileHash -LiteralPath $entry.Dest -Algorithm SHA256).Hash -ne
                (Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash) {
                throw "Deployed file hash mismatch: $($entry.Dest)"
            }
        }
    } catch {
        $cause = $_
        for ($i = $attempted.Count - 1; $i -ge 0; $i--) {
            $entry = $attempted[$i]
            try {
                if ($entry.Existed) { Copy-TgReplacing $entry.Saved $entry.Dest }
                elseif (Test-Path -LiteralPath $entry.Dest -PathType Leaf) { Remove-Item -LiteralPath $entry.Dest -Force }
            } catch { Write-Warning "Could not roll back $($entry.Dest); backup: $($entry.Saved)" }
        }
        throw $cause
    }
    Write-Host "Deployed $Mod ($Runtime). Existing overwritten files were backed up under $backup"
    foreach ($marker in @("BepInEx/$Mod/disable-autoload.txt", 'BepInEx/TGGraphicsBridge/disable-autoload.txt')) {
        if (Test-Path -LiteralPath (Join-Path $GameDir $marker)) {
            Write-Warning "$marker exists: this mod's native DLL is intentionally not loaded. This deployment does not remove it."
        }
    }
}
