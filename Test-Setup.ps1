# SPDX-License-Identifier: MIT
# Checks the installed layout of the mods built on TG Graphics Bridge (each bundles it), without
# requiring developer artifacts.
[CmdletBinding()]
param(
    [string]$GameDir='C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA',
    [ValidateSet('Mono','IL2CPP')][string]$Runtime='Mono'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'tools\Package.Common.ps1')
$game=(Resolve-Path -LiteralPath $GameDir).Path
Assert-TgGame $game $Runtime
$installed=@()
foreach ($mod in (Get-TgModNames)) {
    $manifestPath=Join-Path $game (Get-TgInstalledManifestPath $mod)
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        # A player manually merging a ZIP can point at the root package manifest instead.
        $manifestPath=Join-Path $game (Get-TgPackageManifestName $mod)
    }
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { continue }
    $manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Test-TgPayload $mod $manifest $game $Runtime -Installed
    $installed+=$mod
    if (Test-Path -LiteralPath (Join-Path $game "BepInEx/$mod/disable-autoload.txt")) {
        Write-Warning "BepInEx/$mod/disable-autoload.txt exists: this mod's native DLL is intentionally not loaded."
    }
}
if ($installed.Count -eq 0) { throw 'Neither TG Frame Generation nor TG Better Upscaling is installed.' }
if (Test-Path -LiteralPath (Join-Path $game 'BepInEx/TGGraphicsBridge/disable-autoload.txt')) {
    Write-Warning 'BepInEx/TGGraphicsBridge/disable-autoload.txt exists: the bridge, and with it both mods, is intentionally not started.'
}
foreach ($relative in @((Get-TgMod 'TGFrameGeneration').Obsolete)) {
    if (Test-Path -LiteralPath (Join-Path $game $relative)) { Write-Warning "$relative is left over from an earlier version; delete it." }
}
Write-Host "Installed file, version and hash checks passed for $($installed -join ', ') ($Runtime / $game)."
Write-Host 'Press Play in Steam normally. No launcher or launch arguments are needed.'
Write-Host 'This does not verify initialization timing, frame generation or upscaling. Read BepInEx/TGGraphicsBridge/Autoload.log and each mod''s Native log.'
