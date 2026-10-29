[CmdletBinding()]
param([switch]$Install)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $repo 'tools/Package.Common.ps1')
$game = 'C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA'
$stage = Join-Path $repo 'build/nr/observer-deploy'
$mods = @('TGFrameGeneration', 'TGBetterUpscaling', 'TGNeuralRendering')
$model = Join-Path $repo 'DLSS-NR-Runtime/nvngx_dlssnr.dll'
$modelHash = 'E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E'
if ($Install) {
    Assert-TgGameClosed
    if (Get-Process -Name 'Starfield' -ErrorAction SilentlyContinue) { throw 'Starfield is running; nothing deployed.' }
    if (Test-Path -LiteralPath (Join-Path $repo 'artifacts/harness/RUNNING')) { throw 'Another coordinator owns the harness; nothing deployed.' }
}
Assert-TgGame $game 'Mono'
Assert-TgFile (Join-Path $game 'Fall of Avalon_Data/Managed/TG.Main.dll')
Assert-TgSignature $model 'NVIDIA'
if ((Get-FileHash -LiteralPath $model).Hash -ne $modelHash) { throw 'The model differs from the tested signed copy.' }
$ptPaths = @('BepInEx/plugins/TGPathTracing/TGPathTracing.dll', 'BepInEx/TGPathTracing/Native/TGPathTracing.Native.dll')
if (-not $Install) {
    $nativeComponents = Get-TgNativeComponents
    $nativeNames = @{TGFrameGeneration='TGFG.Native.dll'; TGBetterUpscaling='TGBetterUpscaling.Native.dll'; TGNeuralRendering='TGNeuralRendering.Native.dll'; TGGraphicsBridge='TGGraphicsBridge.Native.dll'}
    $allFiles = @()
    foreach ($mod in $mods) {
        $definition = Get-TgMod $mod
        $files = @()
        foreach ($entry in $definition.Managed) {
            $assembly = $entry.Assembly
            $source = Join-Path $repo "src/$assembly/bin/Release/net472/$assembly.dll"
            Assert-TgManagedVersion $source $assembly '1.2.3.0'
            $files += @{Source=$source; Relative=$entry.Relative}
        }
        foreach ($entry in $definition.Natives) {
            $source = Join-Path $repo ('build/nr/install/' + $nativeComponents[$entry.Component])
            $built = Join-Path $repo ('build/nr/' + $nativeNames[$entry.Component])
            Assert-TgAmd64 $source
            if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $built).Hash) { throw "Stale native install: $source" }
            $files += @{Source=$source; Relative=$entry.Relative}
        }
        foreach ($group in $definition.Signed) {
            foreach ($name in @($group.Files) + @($group.Optional) + @($group.Notices)) {
                $relative = "BepInEx/$mod/Native/$($group.Folder)/$name"
                $source = Join-Path $game $relative
                if ($mod -eq 'TGNeuralRendering') { $source = Join-Path $repo 'DLSS-SDK-310.9.1/LICENSE.txt' }
                if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
                    if ($group.Optional -contains $name) { continue }
                    throw "Missing runtime: $source"
                }
                $files += @{Source=$source; Relative=$relative}
            }
        }
        $files += @{Source=(Join-Path $repo $definition.Extension); Relative=(Get-TgExtensionPath $mod)}
        $records = @()
        foreach ($file in $files) {
            $destination = Get-TgOwnedPath $mod $stage $file.Relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            Copy-Item -LiteralPath $file.Source -Destination $destination -Force
            $hash = (Get-FileHash -LiteralPath $file.Source).Hash
            if ($hash -ne (Get-FileHash -LiteralPath $destination).Hash) { throw "Stage mismatch: $destination" }
            $records += [ordered]@{path=$file.Relative; sha256=$hash}
            $allFiles += [ordered]@{source=$file.Source; path=$file.Relative; sha256=$hash}
        }
        $manifest = [ordered]@{mod=$mod; title=$definition.Title; version='1.2.3'; runtime='Mono'; framework='net472'; abi=(Get-TgAbi); bridge='1.2.3'; utc=[DateTime]::UtcNow.ToString('o'); rendering_validated=$false; files=$records}
        $manifestPath = Join-Path $stage (Get-TgPackageManifestName $mod)
        $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
        Test-TgPayload $mod (Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json) $stage 'Mono'
        Write-Output "Prepared and validated $mod ($($records.Count) files)"
    }
    $pt = @($ptPaths | ForEach-Object { [ordered]@{path=$_; sha256=(Get-FileHash -LiteralPath (Join-Path $game $_)).Hash} })
    [ordered]@{utc=[DateTime]::UtcNow.ToString('o'); version='1.2.3'; game=$game; files=$allFiles; path_tracing=$pt; model_sha256=$modelHash} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $stage 'deployment-plan.json') -Encoding UTF8
    exit 0
}
$plan = Get-Content -LiteralPath (Join-Path $stage 'deployment-plan.json') -Raw | ConvertFrom-Json
foreach ($file in $plan.files) {
    if ((Get-FileHash -LiteralPath $file.source).Hash -ne $file.sha256) { throw "Source changed after staging: $($file.source)" }
}
foreach ($mod in $mods) {
    $manifest = Get-Content -LiteralPath (Join-Path $stage (Get-TgPackageManifestName $mod)) -Raw | ConvertFrom-Json
    Test-TgPayload $mod $manifest $stage 'Mono'
}
# Snapshot PT immediately before installing, so parallel PT deployments are never overwritten.
$ptBefore = @($ptPaths | ForEach-Object { [ordered]@{path=$_; sha256=(Get-FileHash -LiteralPath (Join-Path $game $_)).Hash} })
$modelDest = Join-Path $game 'BepInEx/TGNeuralRendering/Native/DLSS/nvngx_dlssnr.dll'
if (Test-Path -LiteralPath $modelDest) {
    $modelBackup = Join-Path $game ('BepInEx/TGNeuralRendering/Backups/' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-model/nvngx_dlssnr.dll')
    New-Item -ItemType Directory -Path (Split-Path -Parent $modelBackup) -Force | Out-Null
    Copy-Item -LiteralPath $modelDest -Destination $modelBackup
}
Assert-TgGameClosed
Copy-TgReplacing $model $modelDest
if ((Get-FileHash -LiteralPath $modelDest).Hash -ne $modelHash) { throw 'Installed model hash mismatch.' }
foreach ($mod in $mods) { Install-TgPayload $mod $stage $game 'Mono' -Together $mods }
foreach ($file in $plan.files) {
    if ((Get-FileHash -LiteralPath (Join-Path $game $file.path)).Hash -ne $file.sha256) { throw "Installed hash mismatch: $($file.path)" }
}
foreach ($file in $ptBefore) {
    if ((Get-FileHash -LiteralPath (Join-Path $game $file.path)).Hash -ne $file.sha256) { throw "PT changed concurrently: $($file.path)" }
}
Assert-TgSignature $modelDest 'NVIDIA'
[ordered]@{utc=[DateTime]::UtcNow.ToString('o'); version='1.2.3'; game=$game; deployed=$true; game_started=$false; files=$plan.files; model_sha256=$modelHash; path_tracing=$ptBefore} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $stage 'deployment-result.json') -Encoding UTF8
Write-Output "Installed and hash-verified $($plan.files.Count) payload files plus the signed NR model. PT binaries preserved."
