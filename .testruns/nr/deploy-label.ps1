$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $repo 'tools/Package.Common.ps1')
$game='C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA'
Assert-TgGameClosed
Assert-TgGame $game 'Mono'
$relative='BepInEx/plugins/TGNeuralRendering/TGNeuralRendering.dll'
$source=Join-Path $repo 'src/TGNeuralRendering/bin/Release/net472/TGNeuralRendering.dll'
$target=Join-Path $game $relative
$metadata=Join-Path $game 'BepInEx/TGNeuralRendering/Install-manifest.json'
Assert-TgManagedVersion $source 'TGNeuralRendering' '1.2.2.0'
$manifest=Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
Test-TgPayload 'TGNeuralRendering' $manifest $game 'Mono' -Installed
$entry=@($manifest.files | Where-Object { $_.path -eq $relative })
if($entry.Count -ne 1) { throw 'Expected one Neural Rendering plugin entry.' }
$hash=(Get-FileHash -LiteralPath $source).Hash
$backup=Join-Path $game ('BepInEx/TGNeuralRendering/Backups/' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-label')
New-Item -ItemType Directory -Path $backup -Force | Out-Null
Copy-Item -LiteralPath $target -Destination (Join-Path $backup 'TGNeuralRendering.dll')
Copy-Item -LiteralPath $metadata -Destination (Join-Path $backup 'Install-manifest.json')
$entry[0].sha256=$hash
$manifest.utc=[DateTime]::UtcNow.ToString('o')
$stagedMetadata=Join-Path $repo 'build/nr/label-install-manifest.json'
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $stagedMetadata -Encoding UTF8
try {
    Assert-TgGameClosed
    Copy-TgReplacing $source $target
    if((Get-FileHash -LiteralPath $target).Hash -ne $hash) { throw 'Plugin hash mismatch.' }
    Copy-TgReplacing $stagedMetadata $metadata
    Test-TgPayload 'TGNeuralRendering' $manifest $game 'Mono' -Installed
} catch {
    Copy-TgReplacing (Join-Path $backup 'TGNeuralRendering.dll') $target
    Copy-TgReplacing (Join-Path $backup 'Install-manifest.json') $metadata
    throw
}
[ordered]@{utc=$manifest.utc; game=$game; version='1.2.2'; files=@([ordered]@{path=$relative;sha256=$hash}); backup=$backup; game_started=$false} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $repo 'build/nr/label-deployment.json') -Encoding UTF8
Write-Output "Installed Neural Rendering label update; SHA256=$hash; backup=$backup"
