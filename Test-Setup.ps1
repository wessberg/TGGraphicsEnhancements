# SPDX-License-Identifier: MIT
# Checks the installed Steam-autoload layout, without requiring developer artifacts.
[CmdletBinding()]
param(
    [string]$GameDir='C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA',
    [ValidateSet('Mono','IL2CPP')][string]$Runtime='Mono'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'tools\Package.Common.ps1')
$game=(Resolve-Path -LiteralPath $GameDir).Path
Assert-TgfgGame $game $Runtime
$manifestPath=Join-Path $game 'BepInEx/TGFrameGeneration/Install-manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    # A player manually merging a ZIP can point at the root package manifest instead.
    $manifestPath=Join-Path $game 'TGFG-Package-manifest.json'
}
Assert-TgfgFile $manifestPath
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
Test-TgfgPayload $manifest $game $Runtime
if (Test-Path -LiteralPath (Join-Path $game 'BepInEx/TGFrameGeneration/disable-autoload.txt')) {
    throw 'Autoload is disabled by BepInEx/TGFrameGeneration/disable-autoload.txt. Move that marker aside to test autoload; this script will not delete it.'
}
Write-Host "Installed file, version and hash checks passed for $Runtime / $game"
Write-Host 'Press Play in Steam normally. No TGFG launcher or launch arguments are needed by this package.'
Write-Host 'This does not verify initialization timing or FG. Start Off and inspect Autoload.log and Native/TGFG.Native.log under BepInEx/TGFrameGeneration.'
