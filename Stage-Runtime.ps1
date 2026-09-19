# SPDX-License-Identifier: MIT
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RuntimeDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$source=(Resolve-Path $RuntimeDirectory).Path
$dest=Join-Path $PSScriptRoot 'artifacts\Native\Streamline'
$required=@('sl.interposer.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll','nvngx_dlssg.dll')
$optional=@('nvapi64.dll')
# The interposer is staged, byte-identical and still NVIDIA-signed, under a private name:
# Streamline-aware injectors (RenoDX DLSS) find and hook a module named sl.interposer.dll.
$renamed=@{'sl.interposer.dll'='TGFG.Streamline.dll'}
# Validate every input before copying any. Use the official matching production
# package, not debug/development DLLs or files collected from unrelated games.
$files=@()
foreach($name in $required+$optional) {
    $path=Join-Path $source $name
    if(-not (Test-Path $path)) {
        if($required -contains $name) { throw "Missing required runtime: $path" }
        continue
    }
    $signature=Get-AuthenticodeSignature $path
    if($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate -or $signature.SignerCertificate.Subject -notmatch 'NVIDIA') {
        throw "Expected a valid NVIDIA production signature: $path ($($signature.Status)). Nothing was staged."
    }
    $files+=Get-Item $path
}
New-Item -ItemType Directory -Force -Path $dest | Out-Null
$manifest=@()
foreach($file in $files) {
    $name=if($renamed.ContainsKey($file.Name)) { $renamed[$file.Name] } else { $file.Name }
    Copy-Item $file.FullName (Join-Path $dest $name) -Force
    $manifest+=[ordered]@{name=$name;source=$file.Name;fileVersion=$file.VersionInfo.FileVersion;sha256=(Get-FileHash $file.FullName -Algorithm SHA256).Hash}
}
foreach($original in $renamed.Keys) {
    $stale=Join-Path $dest $original
    if(Test-Path $stale) { Remove-Item $stale -Force } # staged by an earlier version under NVIDIA's name
}
$manifest | ConvertTo-Json | Set-Content (Join-Path $dest 'runtime-manifest.json') -Encoding UTF8
Write-Host "Staged signed runtime files to $dest. Nothing was copied into the game folder."
