# SPDX-License-Identifier: MIT
# Developer workflow; nothing in this script launches the game. Builds the managed assemblies of the
# mods (TG Frame Generation, TG Better Upscaling and, for Mono, TG Path Tracing, each with its copy of TG
# Graphics Bridge) and stages them per runtime under artifacts/<Runtime>; -Package or -Deploy continues
# with Package.ps1.
[CmdletBinding()]
param(
    [ValidateSet('Mono','IL2CPP','Both')][string]$Runtime = 'Mono',
    [ValidateSet('All','TGFrameGeneration','TGBetterUpscaling','TGPathTracing')][string]$Mod = 'All',
    [string]$GameDirMono = '',
    [string]$GameDirIl2Cpp = '',
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [switch]$Package,
    [switch]$Deploy,
    # Used by the csproj's DeployToGame target after MSBuild already built the DLLs.
    [switch]$StageOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
& (Join-Path $PSScriptRoot 'tools\Test-Scripts.ps1')
. (Join-Path $PSScriptRoot 'tools\Package.Common.ps1')
if (-not $StageOnly -and -not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw 'Install a .NET SDK capable of targeting net6.0 and net472.'
}
if ($Deploy) { Assert-TgGameClosed }
$version = Get-TgVersion
# Each mod's project references the bridge's library and builds its own copy of the bridge's preloader:
# building the two builds all five assemblies. Every staged build carries both mods.
$projects = [ordered]@{ TGFrameGeneration='src\TGFrameGeneration\TGFrameGeneration.csproj'; TGBetterUpscaling='src\TGBetterUpscaling\TGBetterUpscaling.csproj'
                       TGPathTracing='src\TGPathTracing\TGPathTracing.csproj' }
$outputs = @{
    'TGGraphicsBridge'           = 'src\TGGraphicsBridge\bin\{0}\{1}\TGGraphicsBridge.dll'
    'TGFrameGeneration.Autoload' = 'src\TGFrameGeneration.Autoload\bin\{0}\{1}\TGFrameGeneration.Autoload.dll'
    'TGBetterUpscaling.Autoload' = 'src\TGBetterUpscaling.Autoload\bin\{0}\{1}\TGBetterUpscaling.Autoload.dll'
    'TGFrameGeneration'          = 'src\TGFrameGeneration\bin\{0}\{1}\TGFrameGeneration.dll'
    'TGBetterUpscaling'          = 'src\TGBetterUpscaling\bin\{0}\{1}\TGBetterUpscaling.dll'
    'TGPathTracing.Autoload'     = 'src\TGPathTracing.Autoload\bin\{0}\{1}\TGPathTracing.Autoload.dll'
    'TGPathTracing'              = 'src\TGPathTracing\bin\{0}\{1}\TGPathTracing.dll'
}
$targets = @(if ($Runtime -eq 'Both') { 'Mono','IL2CPP' } else { $Runtime })
foreach ($target in $targets) {
    $framework = Get-TgFramework $target
    if (-not $StageOnly) {
        foreach ($name in $projects.Keys) {
            if (-not (Test-TgModRuntime $name $target)) { continue }
            $project = $projects[$name]
            # Limit restore to the requested framework so Mono does not require IL2CPP.
            $arguments = @('build', (Join-Path $PSScriptRoot $project), '-c', $Configuration, '-f', $framework,
                           "-p:TargetFrameworks=$framework", '-p:DeployToGame=false', '--nologo')
            if ($GameDirMono) { $arguments += "-p:GameDirMono=$GameDirMono" }
            if ($GameDirIl2Cpp) { $arguments += "-p:GameDirIl2Cpp=$GameDirIl2Cpp" }
            & dotnet @arguments
            if ($LASTEXITCODE -ne 0) { throw "Managed build failed: $project ($target). No deployment attempted." }
        }
    }
    $context = Join-Path $PSScriptRoot ("src\TGGraphicsBridge\bin\$Configuration\$framework\TG.GameDir.txt")
    Assert-TgFile $context
    $game = (Get-Content -LiteralPath $context -Raw).Trim()
    if (-not $game) { throw "Empty evaluated game path: $context" }
    $stage = Join-Path $PSScriptRoot "artifacts\$target"
    # Only this build's files: nothing an earlier layout staged (1.0.x's TGFG.Autoload.dll) survives.
    if (Test-Path -LiteralPath (Join-Path $stage 'BepInEx')) { Remove-Item -LiteralPath (Join-Path $stage 'BepInEx') -Recurse -Force }
    $managed = [ordered]@{}
    foreach ($name in (Get-TgModNames)) {
        if (-not (Test-TgModRuntime $name $target)) { continue }
        foreach ($entry in (Get-TgMod $name).Managed) {
            $source = Join-Path $PSScriptRoot ($outputs[$entry.Assembly] -f $Configuration, $framework)
            Assert-TgManagedVersion $source $entry.Assembly "$version.0"
            $dest = Join-Path $stage $entry.Relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force | Out-Null
            Copy-Item -LiteralPath $source -Destination $dest -Force
            $managed[$entry.Relative] = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash
        }
    }
    [ordered]@{
        utc = [DateTime]::UtcNow.ToString('o'); version = $version; runtime = $target
        framework = $framework; configuration = $Configuration; game_dir = $game
        abi = (Get-TgAbi); managed_sha256 = $managed; rendering_validated = $false
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $stage 'build-manifest.json') -Encoding UTF8
    Write-Host "Built/staged ${target}: $(@(Get-TgModNames | Where-Object { Test-TgModRuntime $_ $target }) -join ', '), each with TG Graphics Bridge -> $stage"
}
if ($Package -or $Deploy) {
    & (Join-Path $PSScriptRoot 'Package.ps1') -Runtime $Runtime -Mod $Mod -Deploy:$Deploy
} else {
    Write-Host 'Managed assemblies only. Use Build.ps1 -Package for complete packages, or -Deploy to package and install them.'
    Write-Host 'Native artifacts are reused from artifacts/Native; Build-Native.ps1 and Stage-Runtime.ps1 remain the native build/staging steps.'
}
