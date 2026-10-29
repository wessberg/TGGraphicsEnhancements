# SPDX-License-Identifier: MIT
# Frees disk space: the in-game runs of artifacts\harness (307 GB after three days, 2026-10-08; 92 % of it raw
# frames), the old packages of artifacts\Packages (62 GB: at every run Package.ps1 leaves a folder and a zip per mod
# there, and reads neither again) and the workers' old builds in %TEMP%\tgb (145 GB in 1,523 folders after six days,
# 2026-10-12, with 8 GB left on the disk). It says what it would do and deletes nothing until -Apply.
#   .\tools\harness\Clear-PtHarnessRuns.ps1                 what would go, and how much space that frees
#   .\tools\harness\Clear-PtHarnessRuns.ps1 -List           the same, with a line for every run and kind of package
#   .\tools\harness\Clear-PtHarnessRuns.ps1 -Apply          does it
#   .\tools\harness\Clear-PtHarnessRuns.ps1 -KeepWhole 3 -Keep 'nr-*','20261008-130029' -Apply
# Runs. A run's age counts from the last time anything in it was written, so a run somebody still works in stays new.
#   - The newest -KeepWhole runs (10) stay as they are.
#   - Older runs lose their raw frames (.raw, .hdrraw, .bin): what analyze.py itself deletes without -KeepFrames, and
#     the buffers the neural rendering captures dump. Pictures, reports, JSON and logs stay, so what the docs point at
#     still opens. The tools that read raw frames (flicker.py, edges_shift.py, mean_colour.py, neural_*.py) can no
#     longer be run on such a run.
#   - Runs older than -DeleteAfterDays (14; 0: never) are deleted whole, unless a doc names them (*.md in the repo's
#     root and under docs\): those are evidence and only lose their raw frames.
# Packages. Of each mod, version and game version the newest -KeepPackages (1; 0: none) stay, folder and zip; the
# older ones are deleted. The zips for Nexus are the copies in artifacts\Nexus, which this never touches. What is not
# named like a package (<mod>-<version>-<Mono or IL2CPP>-<time>-<id>) is left alone.
# Workers' builds (the owner, 2026-10-12: "delete all the old ones, and make sure our Clear-PtHarnessRuns.ps1 tool
# also does that"). %TEMP%\tgb is where the workers' build trees, install folders, test pictures and logs go
# (docs/AGENT-WORKERS.md); nothing reads them again once a job is merged. A folder or a loose file there goes when
# nothing in it was written for -WorkerDays days (2; 0: never). A link found inside one is unlinked, never followed
# (a tree copy's junctions lead to the real SDK folders). The tree copies themselves (%TEMP%\tg-agents\wt) are
# sources, some never merged: this never touches them.
# -Keep names runs, packages and workers' folders that are never touched (wildcards allowed).
# Never touched: harness\builds (Deploy-PtBuild.ps1's records and the files it can put back), harness\_config-backup
# (the owner's configs while a run lasts), the RUNNING and STOP files, and a run or package written to in the last 15
# minutes. While a harness run is going (RUNNING names a live process) -Apply deletes nothing and exits 3: deleting
# this much disturbs the disk the run measures its frame times on.
[CmdletBinding()]
param(
    [switch]$Apply,
    [int]$KeepWhole = 10,
    [int]$DeleteAfterDays = 14,
    [int]$KeepPackages = 1,
    [string[]]$Keep = @(),
    [double]$WorkerDays = 2,
    [string]$WorkerRoot = (Join-Path $env:TEMP 'tgb'),
    [switch]$List
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$harnessRoot = Join-Path $repo 'artifacts\harness'
$packagesRoot = Join-Path $repo 'artifacts\Packages'
if ($KeepWhole -lt 0 -or $DeleteAfterDays -lt 0 -or $KeepPackages -lt 0 -or $WorkerDays -lt 0) { throw '-KeepWhole, -DeleteAfterDays, -KeepPackages and -WorkerDays cannot be negative.' }
$frameTypes = @('.raw', '.hdrraw', '.bin')
# Package.ps1's names: TGBetterUpscaling-1.2.1-IL2CPP-20261006-100346-113-e0d718d8, as a folder and as a .zip.
$packageName = '^(?<kind>[A-Za-z]+-[\d.]+-(Mono|IL2CPP))-(?<time>\d{8}-\d{6}-\d{3})-[0-9a-f]{8}$'
$busyMinutes = 15
$now = [DateTime]::Now

# The lock of Run-PtHarness.ps1: its first word is the run's process. A lock whose process is gone is stale.
$lockFile = Join-Path $harnessRoot 'RUNNING'
$holder = 0
if (Test-Path -LiteralPath $lockFile) {
    [int]::TryParse(((Get-Content -LiteralPath $lockFile -ErrorAction SilentlyContinue | Select-Object -First 1) -split ' ')[0], [ref]$holder) | Out-Null
    if ($holder -ne 0 -and -not (Get-Process -Id $holder -ErrorAction SilentlyContinue)) { $holder = 0 }
}

function Measure-Folder([IO.DirectoryInfo]$Dir) {
    $bytes = 0L; $frameBytes = 0L; $written = [DateTime]::MinValue
    foreach ($file in $Dir.EnumerateFiles('*', [IO.SearchOption]::AllDirectories)) {
        $bytes += $file.Length
        if ($frameTypes -contains $file.Extension.ToLowerInvariant()) { $frameBytes += $file.Length }
        if ($file.LastWriteTime -gt $written) { $written = $file.LastWriteTime }
    }
    if ($written -eq [DateTime]::MinValue) { $written = $Dir.LastWriteTime }
    [pscustomobject]@{ Bytes = $bytes; FrameBytes = $frameBytes; Written = $written }
}
# Every folder is a run, whoever made it (some come from other scripts and have no request.json), except the two that
# are not runs. A link to somewhere else is left alone: its files are not this folder's to delete.
function Get-Runs {
    if (-not (Test-Path -LiteralPath $harnessRoot)) { return }
    $found = foreach ($dir in Get-ChildItem -LiteralPath $harnessRoot -Directory -Force) {
        if ($dir.Name -eq 'builds' -or $dir.Name.StartsWith('_') -or ($dir.Attributes -band [IO.FileAttributes]::ReparsePoint)) { continue }
        $size = Measure-Folder $dir
        [pscustomobject]@{ Name = $dir.Name; Path = $dir.FullName; Bytes = $size.Bytes; FrameBytes = $size.FrameBytes; Written = $size.Written; Action = 'stays'; Why = ''; Frees = 0L }
    }
    $found | Sort-Object Written -Descending
}
# A package is its folder and its zip, whichever of the two are there.
function Get-Packages {
    if (-not (Test-Path -LiteralPath $packagesRoot)) { return }
    $found = @{}
    foreach ($item in Get-ChildItem -LiteralPath $packagesRoot -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
        if (-not $item.PSIsContainer -and $item.Extension -ne '.zip') { continue }
        $name = if ($item.PSIsContainer) { $item.Name } else { [IO.Path]::GetFileNameWithoutExtension($item.Name) }
        $match = [regex]::Match($name, $packageName)
        if (-not $match.Success) { continue }
        if (-not $found.ContainsKey($name)) {
            $found[$name] = [pscustomobject]@{ Name = $name; Kind = $match.Groups['kind'].Value; Time = $match.Groups['time'].Value; Paths = @(); Bytes = 0L; Written = [DateTime]::MinValue; Action = 'stays'; Frees = 0L }
        }
        $package = $found[$name]
        $size = if ($item.PSIsContainer) { Measure-Folder $item } else { [pscustomobject]@{ Bytes = $item.Length; Written = $item.LastWriteTime } }
        $package.Paths += $item.FullName
        $package.Bytes += $size.Bytes
        if ($size.Written -gt $package.Written) { $package.Written = $size.Written }
    }
    $found.Values | Sort-Object Kind, @{ Expression = 'Time'; Descending = $true }
}
# A worker's folder, walked without following a link: its size, the last time anything in it was written (its own
# making where it is empty) and the links found in it.
function Measure-WorkerFolder([IO.DirectoryInfo]$Dir) {
    $bytes = 0L; $written = [DateTime]::MinValue; $links = New-Object System.Collections.Generic.List[string]
    $stack = New-Object System.Collections.Generic.Stack[IO.DirectoryInfo]
    $stack.Push($Dir)
    while ($stack.Count -gt 0) {
        $here = $stack.Pop()
        try {
            foreach ($file in $here.GetFiles()) { $bytes += $file.Length; if ($file.LastWriteTime -gt $written) { $written = $file.LastWriteTime } }
            foreach ($sub in $here.GetDirectories()) {
                if ($sub.Attributes -band [IO.FileAttributes]::ReparsePoint) { $links.Add($sub.FullName) } else { $stack.Push($sub) }
            }
        } catch { }
    }
    if ($written -eq [DateTime]::MinValue) { $written = $Dir.CreationTime }
    [pscustomobject]@{ Bytes = $bytes; Written = $written; Links = $links }
}
# Every folder and loose file of the workers' root. A link at the top is left alone.
function Get-WorkerItems {
    if ($WorkerDays -le 0 -or -not (Test-Path -LiteralPath $WorkerRoot)) { return }
    foreach ($item in Get-ChildItem -LiteralPath $WorkerRoot -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
        if ($item.PSIsContainer) {
            $size = Measure-WorkerFolder $item
            [pscustomobject]@{ Name = $item.Name; Path = $item.FullName; Folder = $true; Bytes = $size.Bytes; Written = $size.Written; Links = $size.Links; Action = 'stays'; Frees = 0L }
        } else {
            [pscustomobject]@{ Name = $item.Name; Path = $item.FullName; Folder = $false; Bytes = $item.Length; Written = $item.LastWriteTime; Links = @(); Action = 'stays'; Frees = 0L }
        }
    }
}
# Deletes a worker's folder: its links unlinked first (rmdir takes the link alone), then the rest.
function Remove-WorkerFolder($Item) {
    foreach ($link in $Item.Links) {
        & cmd /c rmdir "`"$link`"" 2>$null | Out-Null
        if (Test-Path -LiteralPath $link) { throw "could not unlink $link" }
    }
    & cmd /c rd /s /q "`"$($Item.Path)`"" 2>$null | Out-Null
    if (Test-Path -LiteralPath $Item.Path) { Remove-Item -LiteralPath $Item.Path -Recurse -Force }
}
function Get-Total($Items, [string]$Property) {
    $sum = 0L
    foreach ($item in @($Items)) { $sum += $item.$Property }
    $sum
}
function Format-GB([long]$Bytes) { '{0:N1} GB' -f ($Bytes / 1GB) }
function Test-Kept([string]$Name) { @($Keep | Where-Object { $Name -like $_ }).Count -gt 0 }

# The docs, read once and only when a run is old enough to be deleted whole. A name counts where it ends there
# ("nr-tavern-retry2" does not name the run nr-tavern-retry); what stands before it is free, as the docs also write
# "Run20261008-112319".
$docs = $null
function Test-NamedInDocs([string]$Name) {
    if ($null -eq $script:docs) {
        $files = @(Get-ChildItem -LiteralPath $repo -Filter '*.md' -File) +
                 @(Get-ChildItem -LiteralPath (Join-Path $repo 'docs') -Filter '*.md' -File -Recurse -ErrorAction SilentlyContinue)
        $script:docs = ($files | ForEach-Object { [IO.File]::ReadAllText($_.FullName) }) -join "`n"
    }
    [regex]::IsMatch($script:docs, [regex]::Escape($Name) + '(?![\w-])', [Text.RegularExpressions.RegexOptions]::IgnoreCase)
}

$runs = @(Get-Runs)
$rank = 0
foreach ($run in $runs) {
    if (Test-Kept $run.Name) { $run.Why = 'named by -Keep'; continue }
    $rank++
    if ($rank -le $KeepWhole) { $run.Why = "one of the newest $KeepWhole"; continue }
    if ($run.Written -gt $now.AddMinutes(-$busyMinutes)) { $run.Why = "written to in the last $busyMinutes minutes"; continue }
    $old = $DeleteAfterDays -gt 0 -and ($now - $run.Written).TotalDays -gt $DeleteAfterDays
    $named = $old -and (Test-NamedInDocs $run.Name)
    if ($named) { $run.Why = 'a doc names it' }
    if ($old -and -not $named) { $run.Action = 'deleted whole'; $run.Frees = $run.Bytes }
    elseif ($run.FrameBytes -gt 0) { $run.Action = 'loses its raw frames'; $run.Frees = $run.FrameBytes }
    elseif (-not $named) { $run.Why = 'no raw frames in it' }
}
$packages = @(Get-Packages)
$kinds = @($packages | Group-Object Kind)
foreach ($kind in $kinds) {
    $rank = 0
    foreach ($package in $kind.Group) {
        if (Test-Kept $package.Name) { continue }
        $rank++
        if ($rank -le $KeepPackages) { continue }
        if ($package.Written -gt $now.AddMinutes(-$busyMinutes)) { continue }
        $package.Action = 'deleted'; $package.Frees = $package.Bytes
    }
}

$workers = @(Get-WorkerItems)
foreach ($item in $workers) {
    if (Test-Kept $item.Name) { continue }
    if (($now - $item.Written).TotalDays -le $WorkerDays) { continue }
    $item.Action = 'deleted'; $item.Frees = $item.Bytes
}
$workersKept = @($workers | Where-Object { $_.Action -eq 'stays' })
$workersGone = @($workers | Where-Object { $_.Action -eq 'deleted' })
$stay = @($runs | Where-Object { $_.Action -eq 'stays' })
$slim = @($runs | Where-Object { $_.Action -eq 'loses its raw frames' })
$gone = @($runs | Where-Object { $_.Action -eq 'deleted whole' })
$packagesKept = @($packages | Where-Object { $_.Action -eq 'stays' })
$packagesGone = @($packages | Where-Object { $_.Action -eq 'deleted' })
$before = (Get-Total $runs 'Bytes') + (Get-Total $packages 'Bytes') + (Get-Total $workers 'Bytes')
Write-Host "$harnessRoot`: $($runs.Count) runs, $(Format-GB (Get-Total $runs 'Bytes'))"
if ($List) {
    foreach ($run in $runs) {
        $what = $run.Action
        if ($run.Why) { $what += " ($($run.Why))" }
        Write-Host ('  {0:yyyy-MM-dd HH:mm}  {1,6:N2} GB  frees {2,6:N2} GB  {3}: {4}' -f $run.Written, ($run.Bytes / 1GB), ($run.Frees / 1GB), $run.Name, $what)
    }
}
Write-Host ('  {0,4} runs stay as they are ({1})' -f $stay.Count, (Format-GB (Get-Total $stay 'Bytes')))
Write-Host ('  {0,4} runs lose their raw frames ({1}); their pictures, reports and logs stay' -f $slim.Count, (Format-GB (Get-Total $slim 'Frees')))
Write-Host ('  {0,4} runs are deleted whole ({1}): older than {2} days and named in no doc' -f $gone.Count, (Format-GB (Get-Total $gone 'Frees')), $DeleteAfterDays)
Write-Host "$packagesRoot`: $($packages.Count) packages, $(Format-GB (Get-Total $packages 'Bytes'))"
if ($List) {
    foreach ($kind in $kinds) {
        $left = @($kind.Group | Where-Object { $_.Action -eq 'stays' } | ForEach-Object { $_.Time })
        Write-Host ('  {0,4} packages  {1,6:N2} GB  frees {2,6:N2} GB  {3}: stays {4}' -f $kind.Count, ((Get-Total $kind.Group 'Bytes') / 1GB), ((Get-Total $kind.Group 'Frees') / 1GB), $kind.Name, $(if ($left.Count -gt 0) { $left -join ', ' } else { 'none' }))
    }
}
Write-Host ('  {0,4} packages stay ({1}): the newest {2} of each mod, version and game version' -f $packagesKept.Count, (Format-GB (Get-Total $packagesKept 'Bytes')), $KeepPackages)
Write-Host ('  {0,4} packages are deleted ({1}), folder and zip' -f $packagesGone.Count, (Format-GB (Get-Total $packagesGone 'Frees')))
if ($WorkerDays -gt 0) {
    Write-Host "$WorkerRoot`: $($workers.Count) workers' folders and files, $(Format-GB (Get-Total $workers 'Bytes'))"
    if ($List) {
        foreach ($item in ($workers | Sort-Object Bytes -Descending | Select-Object -First 60)) {
            Write-Host ('  {0:yyyy-MM-dd HH:mm}  {1,6:N2} GB  {2}: {3}' -f $item.Written, ($item.Bytes / 1GB), $item.Name, $item.Action)
        }
    }
    Write-Host ('  {0,4} stay ({1}): written to in the last {2} days, or named by -Keep' -f $workersKept.Count, (Format-GB (Get-Total $workersKept 'Bytes')), $WorkerDays)
    Write-Host ('  {0,4} are deleted ({1}): nothing in them was written for {2} days' -f $workersGone.Count, (Format-GB (Get-Total $workersGone 'Frees')), $WorkerDays)
}
$frees = (Get-Total $runs 'Frees') + (Get-Total $packages 'Frees') + (Get-Total $workers 'Frees')

if (-not $Apply) {
    Write-Host "This would free $(Format-GB $frees). Nothing was deleted: run it again with -Apply."
    if ($holder -ne 0) { Write-Host "A harness run is going now (process $holder): -Apply refuses until it has ended." }
    exit 0
}
if ($holder -ne 0) {
    Write-Host "A harness run is going (process $holder, $lockFile). Nothing was deleted: run this again when it has ended."
    exit 3
}

$failed = @()
foreach ($path in @($gone | ForEach-Object { $_.Path }) + @($packagesGone | ForEach-Object { $_.Paths })) {
    try { Remove-Item -LiteralPath $path -Recurse -Force } catch { $failed += "$path`: $($_.Exception.Message)" }
}
foreach ($run in $slim) {
    foreach ($file in (New-Object IO.DirectoryInfo $run.Path).GetFiles('*', [IO.SearchOption]::AllDirectories)) {
        if ($frameTypes -notcontains $file.Extension.ToLowerInvariant()) { continue }
        try { $file.Delete() } catch { $failed += "$($file.FullName): $($_.Exception.Message)" }
    }
}
foreach ($item in $workersGone) {
    try { if ($item.Folder) { Remove-WorkerFolder $item } else { Remove-Item -LiteralPath $item.Path -Force } }
    catch { $failed += "$($item.Path): $($_.Exception.Message)" }
}
# What is gone is measured, not assumed: a file somebody holds open stays, and is named below.
$runsAfter = Get-Total @(Get-Runs) 'Bytes'
$packagesAfter = Get-Total @(Get-Packages) 'Bytes'
$workersAfter = Get-Total @(Get-WorkerItems) 'Bytes'
$drive = New-Object IO.DriveInfo ([IO.Path]::GetPathRoot($repo))
Write-Host "Freed $(Format-GB ($before - $runsAfter - $packagesAfter - $workersAfter)). The runs now take $(Format-GB $runsAfter), the packages $(Format-GB $packagesAfter) and the workers' folders $(Format-GB $workersAfter); $($drive.Name) has $(Format-GB $drive.AvailableFreeSpace) free."
if ($failed.Count -gt 0) {
    Write-Host "$($failed.Count) could not be deleted (in use?):"
    $failed | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" }
    exit 1
}
