# SPDX-License-Identifier: MIT
# Filesystem packaging tests with FAKE binaries and mocked binary validation.
# No GPU DLL is loaded, no game is started, and no real game folder is used.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools/Package.Common.ps1')
$realAssertGame=${function:Assert-TgGame}
# Deliberately replace platform/binary checks only within this test script's scope.
function Assert-TgManagedVersion([string]$Path,[string]$Name,[string]$Version) { Assert-TgFile $Path }
function Assert-TgAmd64([string]$Path) { Assert-TgFile $Path }
function Assert-TgSignature([string]$Path,[string]$Signer) { Assert-TgFile $Path }
function Assert-TgGameClosed { }
function Assert-TgGame([string]$GameDir,[string]$Runtime) { }
$tests=0
function Assert-True([bool]$Condition,[string]$Message) {
    if(-not $Condition) { throw $Message }; $script:tests++
}
function Assert-Throws([scriptblock]$Action,[string]$Message) {
    $threw=$false; try { & $Action } catch { $threw=$true }
    Assert-True $threw $Message
}
function New-Fixture([string]$Mod,[string]$Stage) {
    $records=@()
    foreach($relative in (Get-TgRequiredPaths $Mod)) {
        $path=Get-TgOwnedPath $Mod $Stage $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        Set-Content -LiteralPath $path -Value ('TEST FIXTURE - NOT A BINARY - '+$relative)
        $records += [ordered]@{path=$relative; sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    }
    $manifest=[ordered]@{mod=$Mod;version=(Get-TgVersion);runtime='Mono';framework='net472';abi=(Get-TgAbi);bridge=(Get-TgVersion);files=$records}
    $manifestPath=Join-Path $Stage (Get-TgPackageManifestName $Mod)
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath
    $manifestPath
}
$temp=Join-Path ([IO.Path]::GetTempPath()) ('tg-package-tests-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $game=Join-Path $temp 'fake-game'
    New-Item -ItemType Directory -Path $game | Out-Null
    $stages=@{}
    foreach($mod in (Get-TgModNames)) {
        $stage=Join-Path $temp "stage-$mod"
        $manifestPath=New-Fixture $mod $stage
        $valid=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        Test-TgPayload $mod $valid $stage 'Mono'; $tests++
        Assert-Throws { Test-TgPayload $mod $valid $stage 'IL2CPP' } "$mod : wrong runtime was accepted."
        Assert-Throws { Get-TgOwnedPath $mod $stage '../outside.dll' } "$mod : path traversal was accepted."
        Assert-Throws { Get-TgOwnedPath $mod $stage 'dxgi.dll' } "$mod : a game proxy was accepted as payload."
        $bad=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $bad.files += $bad.files[0]
        Assert-Throws { Test-TgPayload $mod $bad $stage 'Mono' } "$mod : duplicate payload was accepted."
        $bad=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $bad.mod='Other'
        Assert-Throws { Test-TgPayload $mod $bad $stage 'Mono' } "$mod : another mod's manifest was accepted."
        $stages[$mod]=$stage
    }
    # Each mod owns only its own paths: one mod's package cannot install into another's folders.
    Assert-Throws { Get-TgOwnedPath 'TGBetterUpscaling' $game 'BepInEx/TGFrameGeneration/Native/TGFG.Native.dll' } 'TG Better Upscaling was allowed to write frame generation files.'
    Assert-Throws { Get-TgOwnedPath 'TGFrameGeneration' $game 'BepInEx/patchers/TGGraphicsBridge.Autoload.dll' } 'Frame generation was allowed to install a preloader shared with another mod.'
    Assert-Throws { Get-TgOwnedPath 'TGBetterUpscaling' $game 'BepInEx/plugins/TGFrameGeneration/TGGraphicsBridge.dll' } "TG Better Upscaling was allowed to write frame generation's copy of the bridge."
    Assert-Throws { Get-TgMod 'TGGraphicsBridge' } 'TG Graphics Bridge is still a package of its own.'
    # Each mod bundles the whole bridge under its own folders...
    foreach($mod in (Get-TgModNames)) {
        foreach($relative in @("BepInEx/plugins/$mod/TGGraphicsBridge.dll","BepInEx/patchers/$mod.Autoload.dll","BepInEx/$mod/Native/TGGraphicsBridge.Native.dll")) {
            Assert-True (@(Get-TgRequiredPaths $mod) -contains $relative) "$mod does not bundle $relative."
        }
    }
    # ...so installing both never deploys the same file twice, which Vortex would report as a conflict.
    $shared=@(Get-TgAllowedPaths 'TGFrameGeneration' | Where-Object { @(Get-TgAllowedPaths 'TGBetterUpscaling') -contains $_ })
    Assert-True ($shared.Count -eq 0) ("Both mods deploy: " + ($shared -join ', '))
    Assert-True (@(Get-TgRequiredPaths 'TGFrameGeneration') -contains 'BepInEx/TGFrameGeneration/graphics-bridge-extension.json') 'Frame generation has no extension manifest.'
    Assert-True (@(Get-TgRequiredPaths 'TGBetterUpscaling') -contains 'BepInEx/TGBetterUpscaling/graphics-bridge-extension.json') 'Upscaling has no extension manifest.'
    Assert-True (@(Get-TgRequiredPaths 'TGBetterUpscaling') -contains 'BepInEx/TGBetterUpscaling/Native/FidelityFX/amd_fidelityfx_upscaler_dx12.dll') 'FSR upscaler not packaged.'
    Assert-True (@(Get-TgRequiredPaths 'TGBetterUpscaling') -contains 'BepInEx/TGBetterUpscaling/Native/XeSS/libxess.dll') 'XeSS upscaler not packaged.'
    foreach($relative in @('BepInEx/TGBetterUpscaling/Native/DLSS/nvngx_dlss.dll','BepInEx/TGBetterUpscaling/Native/DLSS/LICENSE-NVIDIA-RTX-SDKs.txt')) {
        Assert-True (@(Get-TgRequiredPaths 'TGBetterUpscaling') -contains $relative) "The game's DLSS DLL is not packaged with its license: $relative"
    }
    # Never into the game folder itself, where the game's own nvngx_dlss.dll (or a DLSS mod's) lives.
    Assert-Throws { Get-TgOwnedPath 'TGBetterUpscaling' $game 'nvngx_dlss.dll' } "TG Better Upscaling was allowed to write the game's nvngx_dlss.dll."
    Assert-True (@(Get-TgRequiredPaths 'TGFrameGeneration') -contains 'BepInEx/TGFrameGeneration/Native/FidelityFX/amd_fidelityfx_framegeneration_dx12.dll') 'FSR frame generation not packaged.'
    Assert-True (@(Get-TgRequiredPaths 'TGFrameGeneration') -notcontains 'BepInEx/patchers/TGFG.Autoload.dll') '0.5.0 preloader is still packaged.'
    # The optional community backend is ours. The proprietary model remains player-supplied.
    Assert-True (@(Get-TgRequiredPaths 'TGNeuralRendering') -contains 'BepInEx/TGNeuralRendering/Native/TGNeuralRendering.nvngx.dll') 'NR community adapter is missing.'
    Assert-Throws { Get-TgOwnedPath 'TGNeuralRendering' $game 'BepInEx/TGNeuralRendering/Native/DLSS/nvngx_dlssnr.dll' } 'Player-supplied NR model was accepted as package payload.'
    $nrManifest=Get-Content -LiteralPath (Join-Path $stages['TGNeuralRendering'] (Get-TgPackageManifestName 'TGNeuralRendering')) -Raw | ConvertFrom-Json
    $nrManifest.files=@($nrManifest.files | Where-Object { $_.path -notlike '*/TGNeuralRendering.nvngx.dll' })
    Assert-Throws { Test-TgPayload 'TGNeuralRendering' $nrManifest $stages['TGNeuralRendering'] 'Mono' } 'NR payload missing its adapter was accepted.'
    # Zip entries use forward slashes; the Nexus downloads have fixed names per mod and game version.
    $stage=$stages['TGBetterUpscaling']
    $zip=Join-Path $temp 'forward-slashes.zip'
    Write-TgVortexOverrides $stage
    New-TgZip $stage @('BepInEx',(Get-TgPackageManifestName 'TGBetterUpscaling'),(Get-TgVortexOverrideName)) $zip
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive=[System.IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $names=@($archive.Entries | ForEach-Object { $_.FullName })
        Assert-True (@($names | Where-Object { $_.Contains('\') }).Count -eq 0) 'A zip entry uses a backslash.'
        Assert-True ($names -contains 'BepInEx/TGBetterUpscaling/Native/TGBetterUpscaling.Native.dll') 'The native DLL is missing from the zip or misnamed.'
        Assert-True ($names -contains 'TGBetterUpscaling-Package-manifest.json') 'The manifest is missing from the zip root.'
        Assert-True ($names -contains 'vortex_override_instructions.json') 'The Vortex override is missing from the zip root.'
        Assert-True (@($names | Where-Object { $_ -like '*INSTALL*' -or $_ -like '*.txt' -and $_ -notlike '*/LICENSE-*' }).Count -eq 0) 'The zip carries instructions.'
    } finally { $archive.Dispose() }
    # Vortex reads the override from the archive root and deploys the payload to the game root instead.
    $overridePath=Join-Path $stage (Get-TgVortexOverrideName)
    $bytes=[IO.File]::ReadAllBytes($overridePath)
    # Node's JSON.parse, which Vortex reads this with, rejects a UTF-8 BOM.
    Assert-True ($bytes.Length -lt 3 -or $bytes[0] -ne 0xEF -or $bytes[1] -ne 0xBB -or $bytes[2] -ne 0xBF) 'The Vortex override starts with a UTF-8 BOM.'
    $override=@(Get-Content -LiteralPath $overridePath -Raw | ConvertFrom-Json)
    Assert-True ($override.Count -eq 1 -and $override[0].type -eq 'setmodtype' -and $override[0].value -eq 'dinput') 'The Vortex override does not set the Engine Injector mod type.'
    # It instructs Vortex only; the mods' own deployment must never copy it into the game folder.
    Assert-Throws { Get-TgOwnedPath 'TGBetterUpscaling' $stage (Get-TgVortexOverrideName) } 'The Vortex override was accepted as a deployable payload path.'
    Assert-True ((Get-TgNexusName 'TGFrameGeneration' 'Mono') -eq 'TGFrameGeneration - mono.zip') 'Wrong Mono Nexus name.'
    Assert-True ((Get-TgNexusName 'TGFrameGeneration' 'IL2CPP') -eq 'TGFrameGeneration.zip') 'Wrong IL2CPP Nexus name.'
    Assert-True ((Get-TgNexusName 'TGBetterUpscaling' 'IL2CPP') -eq 'TGBetterUpscaling.zip') 'Wrong upscaling Nexus name.'
    # NVIDIA's, AMD's and Intel's license terms require their notices next to their runtimes.
    $fgManifest=Join-Path $stages['TGFrameGeneration'] (Get-TgPackageManifestName 'TGFrameGeneration')
    $bad=Get-Content -LiteralPath $fgManifest -Raw | ConvertFrom-Json
    $bad.files=@($bad.files | Where-Object { $_.path -notlike '*/Streamline/LICENSE-NVIDIA-RTX-SDKs.txt' })
    Assert-Throws { Test-TgPayload 'TGFrameGeneration' $bad $stages['TGFrameGeneration'] 'Mono' } 'A DLSS Frame Generation runtime without its license notice was accepted.'
    $upManifest=Join-Path $stages['TGBetterUpscaling'] (Get-TgPackageManifestName 'TGBetterUpscaling')
    $bad=Get-Content -LiteralPath $upManifest -Raw | ConvertFrom-Json
    $bad.files=@($bad.files | Where-Object { $_.path -notlike '*/XeSS/LICENSE-XeSS.txt' })
    Assert-Throws { Test-TgPayload 'TGBetterUpscaling' $bad $stages['TGBetterUpscaling'] 'Mono' } 'A XeSS runtime without its license notice was accepted.'
    $valid=Get-Content -LiteralPath $upManifest -Raw | ConvertFrom-Json
    $file=Get-TgOwnedPath 'TGBetterUpscaling' $stages['TGBetterUpscaling'] $valid.files[0].path
    $original=[IO.File]::ReadAllBytes($file)
    Add-Content -LiteralPath $file -Value 'tampered'
    Assert-Throws { Test-TgPayload 'TGBetterUpscaling' $valid $stages['TGBetterUpscaling'] 'Mono' } 'Tampered payload was accepted.'
    [IO.File]::WriteAllBytes($file,$original)
    # An existing file is backed up; unrelated proxy/config survives; 0.5.0's preloader and the interposer
    # under NVIDIA's name are backed up and removed.
    $old=Join-Path $game 'BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll'
    New-Item -ItemType Directory -Path (Split-Path -Parent $old) -Force | Out-Null
    Set-Content -LiteralPath $old -Value 'old plugin fixture'
    $oldHash=(Get-FileHash -LiteralPath $old -Algorithm SHA256).Hash
    $proxy=Join-Path $game 'dxgi.dll'; Set-Content -LiteralPath $proxy -Value 'unrelated proxy fixture'
    $proxyHash=(Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash
    $obsolete=@()
    foreach($relative in (Get-TgMod 'TGFrameGeneration').Obsolete) {
        $path=Join-Path $game $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        Set-Content -LiteralPath $path -Value ('obsolete fixture '+$relative)
        $obsolete += @{Path=$path; Hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    }
    # A hard link into another folder (how Vortex deploys) must not be written through.
    $vortex=Join-Path $temp 'vortex-staging/TGBetterUpscaling.dll'
    New-Item -ItemType Directory -Path (Split-Path -Parent $vortex) -Force | Out-Null
    Set-Content -LiteralPath $vortex -Value 'vortex staging copy'
    $vortexHash=(Get-FileHash -LiteralPath $vortex -Algorithm SHA256).Hash
    $linked=Join-Path $game 'BepInEx/plugins/TGBetterUpscaling/TGBetterUpscaling.dll'
    New-Item -ItemType Directory -Path (Split-Path -Parent $linked) -Force | Out-Null
    New-Item -ItemType HardLink -Path $linked -Target $vortex | Out-Null
    foreach($mod in (Get-TgModNames)) { Install-TgPayload $mod $stages[$mod] $game 'Mono' -Together (Get-TgModNames) }
    # Deployed alone, one mod may not sit beside another build of the same bridge version.
    Assert-Throws { Install-TgPayload 'TGBetterUpscaling' $stages['TGBetterUpscaling'] $game 'Mono' } 'A second build of the same bridge version was deployed beside the first.' 
    Assert-True ((Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash -eq $proxyHash) 'Proxy changed.'
    Assert-True ((Get-FileHash -LiteralPath $vortex -Algorithm SHA256).Hash -eq $vortexHash) 'Deployment wrote through a Vortex hard link.'
    foreach($entry in $obsolete) { Assert-True (-not (Test-Path -LiteralPath $entry.Path)) "Obsolete $($entry.Path) was not removed." }
    foreach($mod in (Get-TgModNames)) {
        Assert-True (Test-Path -LiteralPath (Join-Path $game (Get-TgInstalledManifestPath $mod))) "$mod installed manifest missing."
    }
    $backupFiles=@(Get-ChildItem -LiteralPath (Join-Path $game 'BepInEx/TGFrameGeneration/Backups') -Recurse -File)
    Assert-True (@($backupFiles | Where-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash -eq $oldHash }).Count -eq 1) 'Old payload was not backed up.'
    foreach($entry in $obsolete) {
        Assert-True (@($backupFiles | Where-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash -eq $entry.Hash }).Count -eq 1) "Obsolete $($entry.Path) was not backed up."
    }
    foreach($mod in (Get-TgModNames)) {
        $installed=Get-Content -LiteralPath (Join-Path $game (Get-TgInstalledManifestPath $mod)) -Raw | ConvertFrom-Json
        Test-TgPayload $mod $installed $game 'Mono'; $tests++
        # A player's install checked on its own terms: another version of the mod is fine.
        $other=Get-Content -LiteralPath (Join-Path $game (Get-TgInstalledManifestPath $mod)) -Raw | ConvertFrom-Json
        $other.version='9.9.9'
        Test-TgPayload $mod $other $game 'Mono' -Installed; $tests++
        Assert-Throws { Test-TgPayload $mod $other $game 'Mono' } "$mod : another version passed the package check." 
    }
    # The installed layout check expects the bridge's plugin once per mod that bundles it, nowhere else.
    $layout=Join-Path $temp 'layout-game'
    foreach($relative in @('Fall of Avalon.exe','UnityPlayer.dll','BepInEx/core/BepInEx.dll')) {
        $path=Join-Path $layout $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        Set-Content -LiteralPath $path -Value 'layout fixture'
    }
    foreach($mod in (Get-TgModNames)) {
        foreach($entry in (Get-TgMod $mod).Managed) {
            $path=Join-Path $layout $entry.Relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
            Set-Content -LiteralPath $path -Value 'layout fixture'
        }
    }
    & $realAssertGame $layout 'Mono'; $tests++
    $stray=Join-Path $layout 'BepInEx/plugins/SomethingElse/TGGraphicsBridge.dll'
    New-Item -ItemType Directory -Path (Split-Path -Parent $stray) -Force | Out-Null
    Set-Content -LiteralPath $stray -Value 'stray copy'
    Assert-Throws { & $realAssertGame $layout 'Mono' } 'A stray copy of the bridge outside both mods was accepted.'
    Write-Host "$tests mocked filesystem package checks passed. Not a binary, signature, Windows startup, or GPU test."
} finally {
    # This is our unique test directory, never a user-supplied game path.
    $resolvedTemp=[IO.Path]::GetFullPath($temp)
    $expectedParent=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if(-not $resolvedTemp.StartsWith($expectedParent,[StringComparison]::OrdinalIgnoreCase) -or
       (Split-Path -Leaf $resolvedTemp) -notmatch '^tg-package-tests-[0-9a-f]{32}$') { throw 'Invalid fixture cleanup path.' }
    Remove-Item -LiteralPath $temp -Recurse -Force
}
