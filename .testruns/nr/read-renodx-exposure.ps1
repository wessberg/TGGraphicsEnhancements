$ErrorActionPreference='Stop'
$review='build/nr/renodx-review/generic-exposure'
New-Item -ItemType Directory -Force -Path $review | Out-Null
$tree=Get-Content -LiteralPath 'build/nr/renodx-review/generic-tree.json' -Raw | ConvertFrom-Json
$revision=$tree.sha
$files=@('codec_gain.hpp','shaders/v6_encode.cs_5_1.hlsl','shaders/v6_exposure_scale.cs_5_1.hlsl','shaders/v6_commit_exposure.cs_5_1.hlsl','shaders/v6_autoscale.cs_5_1.hlsl','shaders/v6_common.hlsli')
foreach ($file in $files) {
    $destination=Join-Path $review ([IO.Path]::GetFileName($file))
    Invoke-WebRequest -UseBasicParsing -Uri "https://raw.githubusercontent.com/PEQHUB/RenoDX-DLSS5-Generic/$revision/$file" -OutFile $destination
}
Write-Output "Read public Generic source revision $revision. This is not the source of the installed ShortFuse build."
