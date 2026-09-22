# SPDX-License-Identifier: MIT
# Shared package checks. Importing this file does not build, copy, or launch anything.
function Assert-TgfgFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing file: $Path" }
}
function Assert-TgfgGameClosed {
    if (Get-Process -Name 'Fall of Avalon' -ErrorAction SilentlyContinue) {
        throw 'Close FoA before deployment. Start normally through Steam afterward.'
    }
}
function Get-TgfgFramework([string]$Runtime) {
    switch ($Runtime) { 'Mono' { 'net472' }; 'IL2CPP' { 'net6.0' }; default { throw "Unknown runtime: $Runtime" } }
}
function Get-TgfgRuntimeNames {
    # TGFG.Streamline.dll is NVIDIA's sl.interposer.dll under a private name (see Stage-Runtime.ps1).
    @('TGFG.Streamline.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll','nvngx_dlssg.dll')
}
# NVIDIA's notices for the Streamline (MIT), DLSS Frame Generation (NVIDIA RTX SDKs) and Reflex runtimes.
function Get-TgfgNvidiaNotices {
    @('LICENSE-Streamline.txt','LICENSE-NVIDIA-RTX-SDKs.txt','LICENSE-NVIDIA-Reflex.txt')
}
# AMD FSR and Intel XeSS runtimes: signed DLLs plus the license notice their terms require alongside.
function Get-TgfgVendorRuntimes {
    @(
        @{Folder='FidelityFX'; Signer='Advanced Micro Devices'; Notice='LICENSE-FidelityFX.md';
          Files=@('amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_framegeneration_dx12.dll')},
        @{Folder='XeSS'; Signer='Intel Corporation'; Notice='LICENSE-XeSS.txt';
          Files=@('libxess_fg.dll','libxell.dll')}
    )
}
# The Nexus Mods download for each game version: the package under a stable name.
function Get-TgfgNexusName([string]$Runtime) {
    switch ($Runtime) { 'Mono' { 'TGFrameGeneration - mono.zip' }; 'IL2CPP' { 'TGFrameGeneration.zip' }; default { throw "Unknown runtime: $Runtime" } }
}
# Vortex picks a mod type to decide where it deploys an archive, and guesses wrong for a BepInEx
# payload: it lands under the game's own mod folder instead of the game root. Vortex 1.14 and later
# read this file from the archive root, and 'dinput' - shown as "Engine Injector" - is the built-in
# mod type that always deploys to the game root. Only the destination changes; the Vortex game
# extension still decides which files to copy, and Vortex ignores the type if the game rejects it.
function Get-TgfgVortexOverrideName { 'vortex_override_instructions.json' }
function Write-TgfgVortexOverrides([string]$Stage) {
    $json = ConvertTo-Json -InputObject @([ordered]@{type='setmodtype'; value='dinput'}) -Depth 3
    # Vortex reads this with Node, whose JSON.parse rejects a UTF-8 BOM.
    [IO.File]::WriteAllText((Join-Path $Stage (Get-TgfgVortexOverrideName)), $json,
        (New-Object Text.UTF8Encoding $false))
}
# Zip entries use forward slashes, as the zip format requires. Compress-Archive in Windows PowerShell
# 5.1 writes backslashes, which some extractors and archive previews show as flat file names.
function New-TgfgZip([string]$Stage, [string[]]$Items, [string]$Destination) {
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
function Get-TgfgObsoletePaths {
    # Installed by earlier versions and superseded by a renamed file; deploy backs them up and removes them.
    @('BepInEx/TGFrameGeneration/Native/Streamline/sl.interposer.dll')
}
function Get-TgfgRequiredPaths {
    @('BepInEx/patchers/TGFG.Autoload.dll',
      'BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll',
      'BepInEx/TGFrameGeneration/Native/TGFG.Native.dll')
    foreach ($name in @(Get-TgfgRuntimeNames) + @(Get-TgfgNvidiaNotices)) { "BepInEx/TGFrameGeneration/Native/Streamline/$name" }
    foreach ($vendor in (Get-TgfgVendorRuntimes)) {
        foreach ($name in @($vendor.Files) + @($vendor.Notice)) { "BepInEx/TGFrameGeneration/Native/$($vendor.Folder)/$name" }
    }
}
function Get-TgfgAllowedPaths {
    Get-TgfgRequiredPaths
    'BepInEx/TGFrameGeneration/Native/Streamline/nvapi64.dll'
}
function Get-TgfgOwnedPath([string]$Root, [string]$Relative) {
    $relative = $Relative.Replace('\','/')
    if (@(Get-TgfgAllowedPaths) -notcontains $relative) { throw "Not a TGFG package payload path: $Relative" }
    Join-Path $Root $relative
}
function Assert-TgfgFields($Object, [string[]]$Fields, [string]$Label) {
    if ($null -eq $Object) { throw "Empty manifest: $Label" }
    foreach ($field in $Fields) {
        if ($Object.PSObject.Properties.Name -notcontains $field) { throw "$Label is missing field: $field" }
    }
}
function Assert-TgfgManagedVersion([string]$Path, [string]$Name, [string]$Version) {
    Assert-TgfgFile $Path
    # Metadata only: no assembly is loaded into the PowerShell execution context.
    $identity = [Reflection.AssemblyName]::GetAssemblyName($Path)
    if ($identity.Name -ne $Name -or $identity.Version.ToString() -ne $Version) {
        throw "Expected $Name $Version, found $identity at $Path. Rebuild the selected runtime."
    }
}
function Assert-TgfgAmd64([string]$Path) {
    Assert-TgfgFile $Path
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
function Assert-TgfgSignature([string]$Path, [string]$Signer) {
    Assert-TgfgAmd64 $Path
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch $Signer) {
        throw "Expected a valid $Signer production signature: $Path ($($signature.Status))"
    }
}
function Assert-TgfgNvidiaSignature([string]$Path) { Assert-TgfgSignature $Path 'NVIDIA' }
function Assert-TgfgGame([string]$GameDir, [string]$Runtime) {
    Assert-TgfgFile (Join-Path $GameDir 'Fall of Avalon.exe')
    Assert-TgfgFile (Join-Path $GameDir 'UnityPlayer.dll')
    $core = if ($Runtime -eq 'Mono') { 'BepInEx/core/BepInEx.dll' } else { 'BepInEx/core/BepInEx.Unity.IL2CPP.dll' }
    Assert-TgfgFile (Join-Path $GameDir $core)
    # Do not delete alternate copies; they may be local development files.
    $checks = @(
        @{Name='TGFG.Autoload.dll'; Expected=(Join-Path $GameDir 'BepInEx/patchers/TGFG.Autoload.dll')},
        @{Name='TGFrameGeneration.dll'; Expected=(Join-Path $GameDir 'BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll')}
    )
    foreach ($folder in @('BepInEx/patchers','BepInEx/plugins')) {
        $scan = Join-Path $GameDir $folder
        if (-not (Test-Path -LiteralPath $scan)) { continue }
        foreach ($check in $checks) {
            foreach ($file in @(Get-ChildItem -LiteralPath $scan -Recurse -File -Filter $check.Name)) {
                if ([IO.Path]::GetFullPath($file.FullName) -ne [IO.Path]::GetFullPath($check.Expected)) {
                    throw "Duplicate/misplaced TGFG assembly: $($file.FullName). Move it outside patchers/plugins first."
                }
            }
        }
    }
}
function Test-TgfgPayload($Manifest, [string]$Root, [string]$Runtime) {
    Assert-TgfgFields $Manifest @('version','runtime','framework','native_version','native_abi','files') 'Package manifest'
    if ($Manifest.version -ne '0.5.0' -or $Manifest.runtime -ne $Runtime -or
        $Manifest.framework -ne (Get-TgfgFramework $Runtime) -or
        $Manifest.native_version -ne '0.5.0' -or $Manifest.native_abi -ne '0x00050000') {
        throw 'Package metadata does not match the selected integrated v0.5.0 / native ABI v5 build.'
    }
    $seen = @{}
    foreach ($entry in @($Manifest.files)) {
        Assert-TgfgFields $entry @('path','sha256') 'Package file entry'
        $relative = ([string]$entry.path).Replace('\','/')
        if ($seen.ContainsKey($relative)) { throw "Duplicate payload entry: $relative" }
        $seen[$relative] = $true
        $path = Get-TgfgOwnedPath $Root $relative
        Assert-TgfgFile $path
        if ([string]$entry.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) {
            throw "Payload differs from its manifest: $path"
        }
    }
    foreach ($required in (Get-TgfgRequiredPaths)) {
        if (-not $seen.ContainsKey($required)) { throw "Missing required payload entry: $required" }
    }
    Assert-TgfgManagedVersion (Join-Path $Root 'BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll') 'TGFrameGeneration' '0.5.0.0'
    Assert-TgfgManagedVersion (Join-Path $Root 'BepInEx/patchers/TGFG.Autoload.dll') 'TGFG.Autoload' '0.5.0.0'
    Assert-TgfgAmd64 (Join-Path $Root 'BepInEx/TGFrameGeneration/Native/TGFG.Native.dll')
    foreach ($entry in @($Manifest.files)) {
        if ($entry.path.Replace('\','/') -like 'BepInEx/TGFrameGeneration/Native/Streamline/*.dll') {
            Assert-TgfgNvidiaSignature (Get-TgfgOwnedPath $Root $entry.path)
        }
    }
    foreach ($vendor in (Get-TgfgVendorRuntimes)) {
        foreach ($name in $vendor.Files) {
            Assert-TgfgSignature (Get-TgfgOwnedPath $Root "BepInEx/TGFrameGeneration/Native/$($vendor.Folder)/$name") $vendor.Signer
        }
    }
}
function Install-TgfgPayload([string]$Stage, [string]$GameDir, [string]$Runtime) {
    Assert-TgfgGameClosed
    Assert-TgfgGame $GameDir $Runtime
    $manifestPath = Join-Path $Stage 'TGFG-Package-manifest.json'
    Assert-TgfgFile $manifestPath
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Test-TgfgPayload $manifest $Stage $Runtime
    $optional = 'BepInEx/TGFrameGeneration/Native/Streamline/nvapi64.dll'
    if ((Test-Path -LiteralPath (Join-Path $GameDir $optional)) -and
        @($manifest.files | ForEach-Object { $_.path.Replace('\','/') }) -notcontains $optional) {
        throw 'Installed TGFG runtime has nvapi64.dll but the new package does not. Stage the intended matching runtime set or move that TGFG-owned optional DLL aside first.'
    }
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $backup = Join-Path $GameDir "BepInEx/TGFrameGeneration/Backups/$stamp-$Runtime"
    $plan = @()
    foreach ($entry in @($manifest.files)) {
        $relative = $entry.path.Replace('\','/')
        $plan += @{Source=(Get-TgfgOwnedPath $Stage $relative); Relative=$relative;
                   Dest=(Get-TgfgOwnedPath $GameDir $relative); Saved=(Join-Path $backup $relative)}
    }
    foreach ($relative in (Get-TgfgObsoletePaths)) {
        $dest = Join-Path $GameDir $relative
        if (Test-Path -LiteralPath $dest -PathType Leaf) {
            $plan += @{Source=$null; Relative=$relative; Dest=$dest; Saved=(Join-Path $backup $relative); Remove=$true}
        }
    }
    # Installed metadata is written last, so a partial copy never certifies a complete update.
    $relative = 'BepInEx/TGFrameGeneration/Install-manifest.json'
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
            New-Item -ItemType Directory -Path (Split-Path -Parent $entry.Dest) -Force | Out-Null
            Copy-Item -LiteralPath $entry.Source -Destination $entry.Dest -Force
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
                if ($entry.Existed) { Copy-Item -LiteralPath $entry.Saved -Destination $entry.Dest -Force }
                elseif (Test-Path -LiteralPath $entry.Dest -PathType Leaf) { Remove-Item -LiteralPath $entry.Dest -Force }
            } catch { Write-Warning "Could not roll back $($entry.Dest); backup: $($entry.Saved)" }
        }
        throw $cause
    }
    Write-Host "Deployed full $Runtime package. Existing overwritten files were backed up under $backup"
    if (Test-Path -LiteralPath (Join-Path $GameDir 'BepInEx/TGFrameGeneration/disable-autoload.txt')) {
        Write-Warning 'disable-autoload.txt still exists: automatic native loading is intentionally disabled. This deployment does not remove it.'
    }
    Write-Host 'Press Play in Steam. No TGFG launcher, launch arguments, or app-ID workaround is required by this package.'
}
