# SPDX-License-Identifier: MIT
# TG Path Tracing from the working tree into the game, for a harness run: the plugin (and with -Native the native DLL)
# built and copied over the installed ones, nothing else. For iterating on a look (AGENTS.md: "build, install, deploy,
# hash-check", the test suites when the owner says it looks good): no package, no zip, the other mods and the bridge
# they bundle untouched. The plugin is compiled against the tree's bridge library and runs on the installed one (the
# bridge's managed API only grows). A release goes through Build.ps1 and Package.ps1 as before.
#   .\tools\harness\Deploy-PtBuild.ps1            the plugin
#   .\tools\harness\Deploy-PtBuild.ps1 -Native    the native DLL too (its shaders), from build\native
#   .\tools\harness\Deploy-PtBuild.ps1 -Restore <folder>   puts back the files a deploy kept aside
# Each deploy keeps the files it replaces under artifacts\harness\builds\<time>\before and writes what it installed
# (hashes) to artifacts\harness\builds\<time>\build.json; the newest is what a harness run then tests.
[CmdletBinding()]
param(
    [switch]$Native,
    # With -Native: install this very DLL, without building (the one a GPU test just passed: a rebuild here relinked the
    # DLL with no source change, 2026-10-06, and installed one the test never ran).
    [string]$NativeFrom = '',
    [string]$Note = '',
    [string]$Restore = '',
    [string]$GameDir = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repo 'tools\Package.Common.ps1')
if (-not $GameDir) {
    $props = Join-Path $repo 'Build.Local.props'
    if (Test-Path -LiteralPath $props) { $GameDir = ([xml](Get-Content -LiteralPath $props -Raw)).Project.PropertyGroup.GameDirMono }
}
if (-not $GameDir -or -not (Test-Path -LiteralPath (Join-Path $GameDir 'Fall of Avalon.exe'))) { throw "No game folder. Pass -GameDir or set GameDirMono in Build.Local.props." }
Assert-TgGameClosed
$targets = [ordered]@{
    plugin = Join-Path $GameDir 'BepInEx\plugins\TGPathTracing\TGPathTracing.dll'
    native = Join-Path $GameDir 'BepInEx\TGPathTracing\Native\TGPathTracing.Native.dll'
}
function Get-Hash12([string]$Path) { if (Test-Path -LiteralPath $Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.Substring(0, 12) } else { 'missing' } }

if ($Restore) {
    foreach ($name in $targets.Keys) {
        $saved = Join-Path $Restore ("before\" + (Split-Path -Leaf $targets[$name]))
        if (Test-Path -LiteralPath $saved) { Copy-TgReplacing $saved $targets[$name]; Write-Host "Put back $name $(Get-Hash12 $targets[$name])" }
    }
    exit 0
}

$stamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss')
$record = Join-Path $repo "artifacts\harness\builds\$stamp"
New-Item -ItemType Directory -Path (Join-Path $record 'before') -Force | Out-Null
$sources = [ordered]@{}
# The plugin: built into its own bin folder (never artifacts\Mono, which holds the staged release builds).
& dotnet build (Join-Path $repo 'src\TGPathTracing\TGPathTracing.csproj') -c Release -f net472 -p:TargetFrameworks=net472 -p:DeployToGame=false --nologo -v q | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'The plugin did not build. Nothing was installed.' }
$sources.plugin = Join-Path $repo 'src\TGPathTracing\bin\Release\net472\TGPathTracing.dll'
if ($Native -and $NativeFrom) {
    Assert-TgFile $NativeFrom
    $sources.native = $NativeFrom
} elseif ($Native) {
    $vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcvars)) { throw "No Visual Studio build tools at $vcvars." }
    $build = Join-Path $repo 'build\native'
    & cmd /c "`"$vcvars`" >nul && cmake --build `"$build`" --target TGPathTracing.Native" | Select-Object -Last 25 | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'The native DLL did not build. Nothing was installed.' }
    $sources.native = Join-Path $build 'TGPathTracing.Native.dll'
}
$installed = [ordered]@{ utc = [DateTime]::UtcNow.ToString('o'); note = $Note; files = [ordered]@{} }
foreach ($name in $sources.Keys) {
    Assert-TgFile $sources[$name]
    if (Test-Path -LiteralPath $targets[$name]) { Copy-Item -LiteralPath $targets[$name] -Destination (Join-Path $record ("before\" + (Split-Path -Leaf $targets[$name]))) -Force }
    $before = Get-Hash12 $targets[$name]
    Copy-TgReplacing $sources[$name] $targets[$name]
    $now = (Get-FileHash -LiteralPath $targets[$name] -Algorithm SHA256).Hash
    if ($now -ne (Get-FileHash -LiteralPath $sources[$name] -Algorithm SHA256).Hash) { throw "The installed $name differs from the build." }
    $installed.files[$name] = [ordered]@{ sha256 = $now; before = $before; source = $sources[$name] }
    Write-Host ("Installed {0}: {1} (was {2})" -f $name, $now.Substring(0, 12), $before)
}
foreach ($name in $targets.Keys) { if (-not $installed.files.Contains($name)) { $installed.files[$name] = [ordered]@{ sha256 = (Get-FileHash -LiteralPath $targets[$name] -Algorithm SHA256).Hash; before = 'unchanged'; source = '' } } }
ConvertTo-Json -InputObject $installed -Depth 4 | Set-Content -LiteralPath (Join-Path $record 'build.json') -Encoding UTF8
Write-Host "Kept the replaced files and build.json in $record. Vortex will call these files changed: do not let it revert them."
