# SPDX-License-Identifier: MIT
# A copy of the working tree per worker, for parallel agents (docs/AGENT-WORKERS.md). The tree holds uncommitted work,
# so git worktrees don't fit: this copies every tracked and untracked-but-not-ignored file plus Build.Local.props into
# <Root>\wt\<name>, links the SDK folders in as junctions (nothing of an SDK is copied), copies artifacts\Native (the
# GPU test's runtime files), and keeps <Root>\wt\<BaseName> as the pristine snapshot the merge compares against.
#   .\tools\agents\New-TreeCopies.ps1 -Names water,glass -BaseName base01 [-Root <folder outside the repo>]
#   .\tools\agents\New-TreeCopies.ps1 -Remove water,base01 [-Root <folder>]
# One base serves every copy made in the same call. A new call needs a new base name: the tree has moved on.
# -Remove deletes copies safely: a copy holds junctions to the real SDK folders, and a plain recursive delete would
# empty those. It unlinks each junction first (rmdir, which removes the link alone), then deletes the copy.
[CmdletBinding(DefaultParameterSetName = 'Make')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Make')][string[]]$Names,
    [Parameter(Mandatory, ParameterSetName = 'Make')][string]$BaseName,
    [Parameter(Mandatory, ParameterSetName = 'Remove')][string[]]$Remove,
    [string]$Root = (Join-Path $env:TEMP 'tg-agents')
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$wt = Join-Path $Root 'wt'
# "a,b" arrives as one string when the script is started with powershell -File: split it.
$Names = @($Names | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$Remove = @($Remove | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($Remove) {
    foreach ($name in $Remove) {
        $dest = Join-Path $wt $name
        if (-not (Test-Path -LiteralPath $dest)) { Write-Host "${name}: not there"; continue }
        $links = @(Get-ChildItem -LiteralPath $dest -Directory -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
        foreach ($link in $links) { & cmd /c rmdir "`"$($link.FullName)`""; if (Test-Path -LiteralPath $link.FullName) { throw "Could not unlink $($link.FullName): nothing deleted." } }
        Remove-Item -LiteralPath $dest -Recurse -Force
        Write-Host "${name}: removed ($($links.Count) junctions unlinked first)"
    }
    return
}
Set-Location $repo
$files = @(git ls-files --cached --others --exclude-standard) | Where-Object { $_ -and (Test-Path -LiteralPath (Join-Path $repo $_) -PathType Leaf) }
# The SDK folders sit gitignored in the repo root (README "Building"): whatever is there under these names.
$sdks = @(Get-ChildItem -LiteralPath $repo -Directory | Where-Object { $_.Name -match '-SDK-|^Streamline-|^RTXDI-Library-|^DXC-' } | ForEach-Object { $_.Name })
# A copy's files are never older than a day by their write time. Windows' Storage Sense sweeps the temp folder of files
# not written for some days whenever the disk is nearly full (the owner's PC: 98 %), and a copied file keeps its
# source's time: on 2026-10-12 03:28 every copy and every base under %TEMP% lost each file last written before
# 10-06 (138 files a tree; two workers' builds broke, a merge would have run against a base with holes). A day back,
# not now: the compiled shader headers of a build made from the copy stay newer than their sources.
$floor = (Get-Date).AddDays(-1)
foreach ($name in @($BaseName) + $Names) {
    $dest = Join-Path $wt $name
    if (Test-Path -LiteralPath $dest) { throw "$dest exists already" }
    foreach ($f in $files) {
        $to = Join-Path $dest $f
        $dir = Split-Path -Parent $to
        if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
        Copy-Item -LiteralPath (Join-Path $repo $f) -Destination $to
        $made = Get-Item -LiteralPath $to
        if ($made.LastWriteTime -lt $floor) { if ($made.IsReadOnly) { $made.IsReadOnly = $false }; $made.LastWriteTime = $floor }
    }
    $props = Join-Path $repo 'Build.Local.props'
    if (Test-Path -LiteralPath $props) { Copy-Item -LiteralPath $props -Destination (Join-Path $dest 'Build.Local.props') }
    if ($name -ne $BaseName) {
        foreach ($s in $sdks) { & cmd /c mklink /J "`"$(Join-Path $dest $s)`"" "`"$(Join-Path $repo $s)`"" | Out-Null }
        New-Item -ItemType Directory -Path (Join-Path $dest 'artifacts') -Force | Out-Null
        $native = Join-Path $repo 'artifacts\Native'
        if (Test-Path -LiteralPath $native) { Copy-Item -LiteralPath $native -Destination (Join-Path $dest 'artifacts\Native') -Recurse }
    }
    Write-Host ("{0}: {1} files" -f $name, $files.Count)
}
Write-Host "Copies in $wt, made $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') from the working tree. SDK junctions: $($sdks -join ', ')."
