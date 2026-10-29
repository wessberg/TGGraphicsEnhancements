$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $repo 'tools/Package.Common.ps1')
$game='C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA'
Assert-TgGameClosed
if (Test-Path -LiteralPath (Join-Path $repo 'artifacts/harness/RUNNING')) { throw 'The shared game harness is busy.' }
if (Get-Process -Name Starfield -ErrorAction SilentlyContinue) { throw 'Starfield is running.' }
Assert-TgGame $game 'Mono'
$files=@(
    @{relative='BepInEx/plugins/TGNeuralRendering/TGNeuralRendering.dll'; source='src/TGNeuralRendering/bin/Release/net472/TGNeuralRendering.dll'},
    @{relative='BepInEx/TGNeuralRendering/Native/TGNeuralRendering.Native.dll'; source='build/nr/install/TGNeuralRendering/TGNeuralRendering.Native.dll'}
)
$metadata=Join-Path $game 'BepInEx/TGNeuralRendering/Install-manifest.json'
$manifest=Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
Test-TgPayload 'TGNeuralRendering' $manifest $game 'Mono' -Installed
$backup=Join-Path $game ('BepInEx/TGNeuralRendering/Backups/' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-tuning')
New-Item -ItemType Directory -Path $backup -Force | Out-Null
Copy-Item -LiteralPath $metadata -Destination (Join-Path $backup 'Install-manifest.json')
foreach ($file in $files) {
    $file.source=Join-Path $repo $file.source
    $file.target=Join-Path $game $file.relative
    $file.backup=Join-Path $backup ([IO.Path]::GetFileName($file.relative))
    $file.hash=(Get-FileHash -LiteralPath $file.source).Hash
    $entry=@($manifest.files | Where-Object { $_.path -eq $file.relative })
    if($entry.Count -ne 1) { throw "Expected one manifest entry: $($file.relative)" }
    Copy-Item -LiteralPath $file.target -Destination $file.backup
    $entry[0].sha256=$file.hash
}
if((Get-FileHash -LiteralPath (Join-Path $repo 'build/nr/TGNeuralRendering.Native.dll')).Hash -ne $files[1].hash) { throw 'Installed native build is stale.' }
$manifest.utc=[DateTime]::UtcNow.ToString('o')
$stagedMetadata=Join-Path $repo 'build/nr/tuning-install-manifest.json'
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $stagedMetadata -Encoding UTF8
try {
    Assert-TgGameClosed
    foreach ($file in $files) {
        Copy-TgReplacing $file.source $file.target
        if((Get-FileHash -LiteralPath $file.target).Hash -ne $file.hash) { throw 'Deployed hash mismatch.' }
    }
    Copy-TgReplacing $stagedMetadata $metadata
    Test-TgPayload 'TGNeuralRendering' $manifest $game 'Mono' -Installed
} catch {
    foreach ($file in $files) { Copy-TgReplacing $file.backup $file.target }
    Copy-TgReplacing (Join-Path $backup 'Install-manifest.json') $metadata
    throw
}
[ordered]@{utc=$manifest.utc; game=$game; version='1.2.3'; files=$files; backup=$backup; game_started=$false} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $repo 'build/nr/tuning-deployment.json') -Encoding UTF8
Write-Output "Installed Neural Rendering controls and placement; two files hash verified; backup=$backup"
