# SPDX-License-Identifier: MIT
# The normal version's test (the Mono harness cannot run there): installs the probe of tools/harness/Il2CppProbe, starts the
# game through the harness's GPU guard, waits for the probe to quit, takes the plugin and the configs back out and checks
# that no save changed. Results: artifacts/il2cpp-probe/<time>. The game is not forced to the front.
[CmdletBinding()]
param([string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA', [int]$Minutes = 10)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'BepInEx\core\BepInEx.Unity.IL2CPP.dll'))) { throw 'The game folder does not hold the normal version with BepInEx 6.' }
if (Get-Process -Name 'Fall of Avalon' -ErrorAction SilentlyContinue) { throw 'Close the game first.' }
$project = Join-Path $PSScriptRoot 'Il2CppProbe\Il2CppProbe.csproj'
& dotnet build $project -c Release -f net6.0 -p:TargetFrameworks=net6.0 --nologo -v q
if ($LASTEXITCODE -ne 0) { throw 'The probe did not build.' }
$built = Join-Path $PSScriptRoot 'Il2CppProbe\bin\Release\net6.0\TGIl2CppProbe.dll'
$out = Join-Path $repo ("artifacts\il2cpp-probe\" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$configs = Join-Path $out 'configs-before'
New-Item -ItemType Directory -Path $configs -Force | Out-Null
$configDir = Join-Path $GameDir 'BepInEx\config'
Get-ChildItem -LiteralPath $configDir -File | Copy-Item -Destination $configs
function Get-SaveHashes {
    $root = Join-Path $env:USERPROFILE 'AppData\LocalLow\Questline\Fall of Avalon'
    $hashes = foreach ($profile in @(Get-ChildItem -LiteralPath $root -Directory)) {
        $saved = Join-Path $profile.FullName 'Saved'
        if (Test-Path -LiteralPath $saved) { foreach ($file in @(Get-ChildItem -LiteralPath $saved -File -Filter '*.zip')) { "$($file.FullName)|$((Get-FileHash -LiteralPath $file.FullName).Hash)" } }
    }
    @($hashes | Sort-Object)
}
$savesBefore = Get-SaveHashes
$installed = Join-Path $GameDir 'BepInEx\plugins\TGIl2CppProbe.dll'
Copy-Item -LiteralPath $built -Destination $installed -Force
try {
    # A game of the owner's that is open holds a start back, whatever it uses just then (AGENTS.md).
    while (Get-Process -Name 'Starfield' -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 5 }
    & (Join-Path $PSScriptRoot 'Run-PtHarness.ps1') -LaunchOnly -GameDir $GameDir
    $deadline = (Get-Date).AddMinutes($Minutes)
    while ((Get-Process -Name 'Fall of Avalon' -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 3 }
    $game = Get-Process -Name 'Fall of Avalon' -ErrorAction SilentlyContinue
    if ($game) { Write-Host 'The probe did not finish in time: closing the game.'; [void]$game.CloseMainWindow(); Start-Sleep -Seconds 15; Get-Process -Name 'Fall of Avalon' -ErrorAction SilentlyContinue | Stop-Process -Force }
    Start-Sleep -Seconds 3
} finally {
    if (Test-Path -LiteralPath $installed) { Remove-Item -LiteralPath $installed -Force }
    Get-ChildItem -LiteralPath $configs -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $configDir $_.Name) -Force }
    $results = Join-Path $GameDir 'BepInEx\TGIl2CppProbe'
    if (Test-Path -LiteralPath $results) { Copy-Item -Path (Join-Path $results '*') -Destination $out -Recurse -Force; Remove-Item -LiteralPath $results -Recurse -Force }
    foreach ($log in @('BepInEx\LogOutput.log', 'BepInEx\ErrorLog.log', 'BepInEx\TGFrameGeneration\Native\TGFG.Native.log', 'BepInEx\TGBetterUpscaling\Native\TGBetterUpscaling.Native.log', 'BepInEx\TGGraphicsBridge\TGGraphicsBridge.Native.log')) {
        $path = Join-Path $GameDir $log
        if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $out -Force }
    }
}
if (Compare-Object $savesBefore (Get-SaveHashes)) { throw 'A save changed during the run.' }
Write-Host "No save changed. Results: $out"
