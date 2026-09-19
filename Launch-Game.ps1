# SPDX-License-Identifier: MIT
# Compatibility entry point only. The separate launcher is no longer the player workflow.
[CmdletBinding()]
param(
    [string]$GameDir='C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA',
    [ValidateSet('Mono','IL2CPP')][string]$Runtime='Mono',
    [string[]]$GameArguments=@()
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if ($GameArguments.Count -ne 0) { throw 'GameArguments are not used by automatic loading. Start the game normally from Steam.' }
Write-Warning 'Launch-Game.ps1 is retired as a launcher. This command checks the installed package only.'
& (Join-Path $PSScriptRoot 'Test-Setup.ps1') -GameDir $GameDir -Runtime $Runtime
Write-Host 'No process was started. Press Play in Steam.'
