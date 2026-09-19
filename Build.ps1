# SPDX-License-Identifier: MIT
# Developer workflow; nothing in this script launches the game.
[CmdletBinding()]
param(
    [ValidateSet('Mono','IL2CPP','Both')][string]$Runtime = 'Mono',
    [string]$GameDirMono = '',
    [string]$GameDirIl2Cpp = '',
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [switch]$Package,
    [switch]$Deploy,
    # Used by the csproj's DeployToGame target after MSBuild already built both DLLs.
    [switch]$StageOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
& (Join-Path $PSScriptRoot 'tools\Test-Scripts.ps1')
. (Join-Path $PSScriptRoot 'tools\Package.Common.ps1')
if (-not $StageOnly -and -not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw 'Install a .NET SDK capable of targeting net6.0 and net472.'
}
if ($Deploy) { Assert-TgfgGameClosed }
$project = Join-Path $PSScriptRoot 'src\TGFrameGeneration\TGFrameGeneration.csproj'
$targets = if ($Runtime -eq 'Both') { @('Mono','IL2CPP') } else { @($Runtime) }
foreach ($target in $targets) {
    $framework = Get-TgfgFramework $target
    if (-not $StageOnly) {
        # ProjectReference builds the matching preloader, without a runtime reference.
        # Limit restore to the requested framework so Mono does not require IL2CPP.
        $arguments = @('build', $project, '-c', $Configuration, '-f', $framework,
                       "-p:TargetFrameworks=$framework", '-p:DeployToGame=false', '--nologo')
        if ($GameDirMono) { $arguments += "-p:GameDirMono=$GameDirMono" }
        if ($GameDirIl2Cpp) { $arguments += "-p:GameDirIl2Cpp=$GameDirIl2Cpp" }
        & dotnet @arguments
        if ($LASTEXITCODE -ne 0) { throw "Managed/preloader build failed: $target. No deployment attempted." }
    }
    $plugin = Join-Path $PSScriptRoot "src\TGFrameGeneration\bin\$Configuration\$framework\TGFrameGeneration.dll"
    $loader = Join-Path $PSScriptRoot "src\TGFG.Autoload\bin\$Configuration\$framework\TGFG.Autoload.dll"
    $context = Join-Path (Split-Path -Parent $plugin) 'TGFG.GameDir.txt'
    Assert-TgfgFile $context
    Assert-TgfgManagedVersion $plugin 'TGFrameGeneration' '0.4.0.0'
    Assert-TgfgManagedVersion $loader 'TGFG.Autoload' '0.4.0.0'
    $game = (Get-Content -LiteralPath $context -Raw).Trim()
    if (-not $game) { throw "Empty evaluated game path: $context" }
    $stage = Join-Path $PSScriptRoot "artifacts\$target"
    $pluginOut = Join-Path $stage 'BepInEx\plugins\TGFrameGeneration\TGFrameGeneration.dll'
    $loaderOut = Join-Path $stage 'BepInEx\patchers\TGFG.Autoload.dll'
    foreach ($pair in @(@{Source=$plugin; Dest=$pluginOut}, @{Source=$loader; Dest=$loaderOut})) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $pair.Dest) -Force | Out-Null
        Copy-Item -LiteralPath $pair.Source -Destination $pair.Dest -Force
    }
    [ordered]@{
        utc = [DateTime]::UtcNow.ToString('o'); version = '0.4.0'; runtime = $target
        framework = $framework; configuration = $Configuration; game_dir = $game
        managed_sha256 = (Get-FileHash -LiteralPath $pluginOut -Algorithm SHA256).Hash
        autoload_sha256 = (Get-FileHash -LiteralPath $loaderOut -Algorithm SHA256).Hash
        native_abi = '0x00040000'; rendering_validated = $false
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'build-manifest.json') -Encoding UTF8
    Write-Host "Built/staged ${target}: gameplay plugin AND autoload preloader -> $stage"
}
if ($Package -or $Deploy) {
    & (Join-Path $PSScriptRoot 'Package.ps1') -Runtime $Runtime -Deploy:$Deploy
} else {
    Write-Host 'Managed pair only. Use Build.ps1 -Package for a complete package, or -Deploy to package and install it.'
    Write-Host 'Native artifacts are reused from artifacts/Native; Build-Native.ps1 and Stage-Runtime.ps1 remain the native build/staging steps.'
}
