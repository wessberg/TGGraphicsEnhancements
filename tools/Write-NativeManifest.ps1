# SPDX-License-Identifier: MIT
# Records the hashes of the staged native DLLs in artifacts/Native/build-manifest.json, which
# Package.ps1 checks before it packages them. Called by Build-Native.ps1 after staging.
[CmdletBinding()]
param(
    [string]$Stage = '',
    [hashtable]$Headers = @{}
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Package.Common.ps1')
# $PSScriptRoot is not set during parameter binding in Windows PowerShell.
if (-not $Stage) { $Stage = Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts\Native' }
$hashes = [ordered]@{}
$components = Get-TgNativeComponents
foreach ($component in $components.Keys) {
    $path = Join-Path $Stage $components[$component]
    # A PT-only build can retain an older NR stage. NR packaging still requires
    # the adapter and its recorded hash through Get-TgNativePayload.
    if ($component -eq 'TGNeuralRenderingCommunity' -and -not (Test-Path -LiteralPath $path)) { continue }
    Assert-TgAmd64 $path
    $hashes[$component] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}
$manifest = [ordered]@{
    utc = [DateTime]::UtcNow.ToString('o')
    architecture = 'x64'
    version = (Get-TgVersion)
    abi = (Get-TgAbi); extension_abi = 1
    backend = 'D3D11 rendering -> shared GPU resources -> private D3D12 device (NVIDIA Streamline DLSS-G, AMD FSR, Intel XeSS)'
    native_sha256 = $hashes
    game_tested = $false
}
foreach ($key in $Headers.Keys) { $manifest[$key] = $Headers[$key] }
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Stage 'build-manifest.json') -Encoding UTF8
Write-Host "Recorded native hashes in $(Join-Path $Stage 'build-manifest.json')."
