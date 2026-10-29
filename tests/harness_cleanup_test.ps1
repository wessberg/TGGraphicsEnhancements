# SPDX-License-Identifier: MIT
# Checks tools\harness\Clear-PtHarnessRuns.ps1 on made-up artifacts folders under %TEMP%: what it deletes and, above
# all, what it must leave. The real artifacts folder is never read or touched, nor the workers' real folder
# (%TEMP%\tgb): every call here names a made-up one (Invoke-Cleanup).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\harness_cleanup_test.ps1
#   ... -Script <path>    checks another copy of the script (a mutant: this test must then fail)
[CmdletBinding()]
param([string]$Script = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $Script) { $Script = Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\harness\Clear-PtHarnessRuns.ps1' }
$failures = @()
$trees = @()
function Check([bool]$Condition, [string]$What) { if (-not $Condition) { $script:failures += $What } }

# A repo of its own, as the script finds the artifacts and the docs from where it lies.
function New-FakeTree {
    $repo = Join-Path ([IO.Path]::GetTempPath()) ('tg-harness-cleanup-' + [Guid]::NewGuid().ToString('N'))
    foreach ($folder in 'tools\harness', 'docs', 'artifacts') { New-Item -ItemType Directory -Path (Join-Path $repo $folder) -Force | Out-Null }
    Copy-Item -LiteralPath $Script -Destination (Join-Path $repo 'tools\harness\Clear-PtHarnessRuns.ps1')
    [IO.File]::WriteAllText((Join-Path $repo 'docs\NOTES.md'), 'The proof: `artifacts/harness/old-named/tavern/overview.jpg`.')
    $script:trees += $repo
    $repo
}
function Add-File([string]$Repo, [string]$Relative, [TimeSpan]$Age) {
    $path = Join-Path $Repo "artifacts\$Relative"
    New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
    [IO.File]::WriteAllBytes($path, (New-Object byte[] 1024))
    (Get-Item -LiteralPath $path).LastWriteTime = [DateTime]::Now - $Age
}
# A run as the harness leaves it with -KeepFrames, last written $Age ago.
function Add-Run([string]$Repo, [string]$Name, [TimeSpan]$Age) {
    foreach ($file in 'request.json', 'report.md', 'tavern\overview.jpg', 'tavern\2-traced-none\frame.jpg', 'tavern\2-traced-none\f00.raw',
                      'tavern\2-traced-none\f00.hdrraw', 'tavern\3-neural\take-00\nr-00.bin') {
        Add-File $Repo "harness\$Name\$file" $Age
    }
}
# A package as Package.ps1 leaves it: the folder and its zip.
function Add-Package([string]$Repo, [string]$Name, [TimeSpan]$Age) {
    foreach ($file in "$Name\BepInEx\plugins\Mod\Mod.dll", "$Name\Mod-Package-manifest.json", "$Name.zip") { Add-File $Repo "Packages\$file" $Age }
}
function Get-Files([string]$Repo) {
    $root = Join-Path $Repo 'artifacts'
    @(Get-ChildItem -LiteralPath $root -Recurse -File -Force | ForEach-Object { $_.FullName.Substring($root.Length + 1) } | Sort-Object)
}
function Invoke-Cleanup([string]$Repo, [hashtable]$Parameters) {
    # The workers' folder is the made-up tree's own, always: the script's default is the real one.
    if (-not $Parameters.ContainsKey('WorkerRoot')) { $Parameters['WorkerRoot'] = Join-Path $Repo 'workers' }
    $global:LASTEXITCODE = 0
    $text = & (Join-Path $Repo 'tools\harness\Clear-PtHarnessRuns.ps1') @Parameters 6>&1 | Out-String
    [pscustomobject]@{ Code = $LASTEXITCODE; Text = $text }
}
function Test-Same($Left, $Right) { (@($Left) -join '|') -eq (@($Right) -join '|') }
$frames = '\.(raw|hdrraw|bin)$'
$hour = [TimeSpan]::FromHours(1); $day = [TimeSpan]::FromDays(1); $tenDays = [TimeSpan]::FromDays(10)

# 1. The runs: the newest stay whole, older runs lose their frames, very old ones go unless a doc names them
# ("old" is the start of the named run's name, and must not pass for it).
$repo = New-FakeTree
Add-Run $repo 'newest' $hour
Add-Run $repo 'second' ($hour + $hour)
Add-Run $repo 'third' ($day + $day + $day)
Add-Run $repo 'old-named' ([TimeSpan]::FromDays(30))
Add-Run $repo 'old' ([TimeSpan]::FromDays(31))
Add-Run $repo 'pinned' ([TimeSpan]::FromDays(40))
Add-File $repo 'harness\builds\20260101-000000\before\TGPathTracing.Native.dll' ([TimeSpan]::FromDays(40))
Add-File $repo 'harness\builds\20260101-000000\dump.bin' ([TimeSpan]::FromDays(40))
Add-File $repo 'harness\_config-backup\com.wessberg.tgpathtracing.cfg' ([TimeSpan]::FromDays(40))
Add-File $repo 'harness\_config-backup\dump.raw' ([TimeSpan]::FromDays(40))
Add-File $repo 'Nexus\TGFrameGeneration - mono.zip' ([TimeSpan]::FromDays(40))
# No process has this number (they are multiples of 4): a stale lock, which holds nothing back.
[IO.File]::WriteAllText((Join-Path $repo 'artifacts\harness\RUNNING'), '999999 20260101-000000 2026-01-01T00:00:00')
$all = Get-Files $repo
$dry = Invoke-Cleanup $repo @{ KeepWhole = 2; Keep = 'pin*' }
Check ($dry.Code -eq 0) "dry run: exit code $($dry.Code)"
Check ($dry.Text -match 'Nothing was deleted') "dry run: does not say that nothing was deleted: $($dry.Text)"
Check (Test-Same $all (Get-Files $repo)) 'dry run: files changed'
$done = Invoke-Cleanup $repo @{ Apply = $true; KeepWhole = 2; Keep = 'pin*' }
$expected = @($all | Where-Object { $_ -notlike 'harness\old\*' -and -not ($_ -match '^harness\\(third|old-named)\\' -and $_ -match $frames) })
Check ($done.Code -eq 0) "runs: exit code $($done.Code): $($done.Text)"
Check ($expected.Count -eq $all.Count - 7 - 6) "the test's own list of runs is wrong: $($expected.Count) of $($all.Count)"
Check (Test-Same $expected (Get-Files $repo)) "runs: left $((Get-Files $repo) -join ', ')"

# 2. With nothing kept whole: a run written to a minute ago stays, and -DeleteAfterDays 0 deletes no run whole.
$repo = New-FakeTree
Add-Run $repo 'busy' ([TimeSpan]::FromMinutes(1))
Add-Run $repo 'idle' $hour
Add-Run $repo 'ancient' ([TimeSpan]::FromDays(100))
$all = Get-Files $repo
$done = Invoke-Cleanup $repo @{ Apply = $true; KeepWhole = 0; DeleteAfterDays = 0 }
$expected = @($all | Where-Object { $_ -like 'harness\busy\*' -or $_ -notmatch $frames })
Check ($done.Code -eq 0) "nothing kept whole: exit code $($done.Code): $($done.Text)"
Check (Test-Same $expected (Get-Files $repo)) "nothing kept whole: left $((Get-Files $repo) -join ', ')"

# 3. While a harness run is going (the lock names a live process: this one) nothing is deleted.
$repo = New-FakeTree
Add-Run $repo 'third' ($day + $day + $day)
Add-Run $repo 'old' ([TimeSpan]::FromDays(31))
Add-Package $repo 'TGFrameGeneration-1.2.0-Mono-20260926-100000-000-aaaaaaaa' $tenDays
Add-Package $repo 'TGFrameGeneration-1.2.0-Mono-20260927-100000-000-bbbbbbbb' $tenDays
[IO.File]::WriteAllText((Join-Path $repo 'artifacts\harness\RUNNING'), "$PID 20261008-224225 2026-10-08T22:42:25")
$all = Get-Files $repo
$done = Invoke-Cleanup $repo @{ Apply = $true; KeepWhole = 0 }
Check ($done.Code -eq 3) "live lock: exit code $($done.Code), expected 3"
Check (Test-Same $all (Get-Files $repo)) 'live lock: files were deleted'

# 4. The packages: of each mod, version and game version the newest stays, folder and zip. What -Keep names and what
# is not named like a package stay too. With none kept, one written to a minute ago still stays.
$repo = New-FakeTree
$mono = 'TGFrameGeneration-1.2.0-Mono'
Add-Package $repo "$mono-20260926-100000-000-aaaaaaaa" $tenDays
Add-Package $repo "$mono-20260927-100000-000-bbbbbbbb" $tenDays
Add-Package $repo "$mono-20260928-100000-000-cccccccc" $tenDays
Add-File $repo "Packages\$mono-20260925-100000-000-dddddddd.zip" $tenDays
Add-Package $repo 'TGFrameGeneration-1.2.0-IL2CPP-20260926-100000-000-eeeeeeee' $tenDays
Add-Package $repo 'TGBetterUpscaling-1.2.1-Mono-20261006-100000-000-ffffffff' $tenDays
Add-Package $repo 'TGBetterUpscaling-1.2.1-Mono-20261008-100000-000-01234567' ([TimeSpan]::FromMinutes(1))
Add-File $repo 'Packages\notes\readme.txt' $tenDays
Add-File $repo 'Packages\something.zip' $tenDays
Add-File $repo 'Packages\other.zip' $tenDays
Add-File $repo 'Nexus\TGFrameGeneration - mono.zip' $tenDays
$all = Get-Files $repo
$dry = Invoke-Cleanup $repo @{ Keep = "$mono-20260926*" }
Check (Test-Same $all (Get-Files $repo)) 'packages, dry run: files changed'
$done = Invoke-Cleanup $repo @{ Apply = $true; Keep = "$mono-20260926*" }
$expected = @($all | Where-Object { $_ -notmatch '-(bbbbbbbb|dddddddd|ffffffff)[\\.]' })
Check ($done.Code -eq 0) "packages: exit code $($done.Code): $($done.Text)"
Check ($expected.Count -eq $all.Count - 3 - 1 - 3) "the test's own list of packages is wrong: $($expected.Count) of $($all.Count)"
Check (Test-Same $expected (Get-Files $repo)) "packages: left $((Get-Files $repo) -join ', ')"
$done = Invoke-Cleanup $repo @{ Apply = $true; KeepPackages = 0 }
$expected = @($all | Where-Object { $_ -notmatch '^Packages\\TG' -or $_ -match '-01234567[\\.]' })
Check ($done.Code -eq 0) "no package kept: exit code $($done.Code): $($done.Text)"
Check (Test-Same $expected (Get-Files $repo)) "no package kept: left $((Get-Files $repo) -join ', ')"

# 5. Nothing there at all: no error.
$repo = New-FakeTree
$done = Invoke-Cleanup $repo @{ Apply = $true }
Check ($done.Code -eq 0) "empty: exit code $($done.Code): $($done.Text)"

# 6. The workers' builds: a folder or a loose file in which nothing was written for the days given goes; one with a
# fresh file in it stays whole, and what -Keep names; an old empty folder goes; a link inside an old folder is
# unlinked and never followed (what it leads to stays); with -WorkerDays 0 nothing there is touched.
function Add-WorkerFile([string]$Repo, [string]$Relative, [TimeSpan]$Age) {
    $path = Join-Path $Repo "workers\$Relative"
    New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
    [IO.File]::WriteAllBytes($path, (New-Object byte[] 1024))
    (Get-Item -LiteralPath $path).LastWriteTime = [DateTime]::Now - $Age
}
function Get-WorkerFiles([string]$Repo) {
    $root = Join-Path $Repo 'workers'
    @(Get-ChildItem -LiteralPath $root -Recurse -Force | ForEach-Object { $_.FullName.Substring($root.Length + 1) } | Sort-Object)
}
$repo = New-FakeTree
$fiveDays = [TimeSpan]::FromDays(5)
Add-WorkerFile $repo 'old-build\CMakeCache.txt' $fiveDays
Add-WorkerFile $repo 'old-build\sub\deep\a.obj' $fiveDays
Add-WorkerFile $repo 'busy-build\x.obj' $fiveDays
Add-WorkerFile $repo 'busy-build\sub\y.obj' $hour
Add-WorkerFile $repo 'kept-build\z.obj' $fiveDays
Add-WorkerFile $repo 'linked\data.txt' $fiveDays
Add-WorkerFile $repo 'old.log' $fiveDays
Add-WorkerFile $repo 'new.log' $hour
$emptyOld = Join-Path $repo 'workers\empty-old'
New-Item -ItemType Directory -Path $emptyOld | Out-Null
(Get-Item -LiteralPath $emptyOld).CreationTime = [DateTime]::Now - $fiveDays
$precious = Join-Path $repo 'precious'
New-Item -ItemType Directory -Path $precious | Out-Null
[IO.File]::WriteAllText((Join-Path $precious 'keep.txt'), 'an SDK the link leads to')
& cmd /c mklink /J "`"$(Join-Path $repo 'workers\linked\sdk')`"" "`"$precious`"" | Out-Null
Check (Test-Path -LiteralPath (Join-Path $repo 'workers\linked\sdk\keep.txt')) "the test's own link was not made"
Add-Run $repo 'newest' $hour
$all = Get-Files $repo
$allWorkers = Get-WorkerFiles $repo
$dry = Invoke-Cleanup $repo @{ Keep = 'kept*' }
Check ($dry.Code -eq 0) "workers, dry run: exit code $($dry.Code): $($dry.Text)"
Check (Test-Same $allWorkers (Get-WorkerFiles $repo)) 'workers, dry run: files changed'
$none = Invoke-Cleanup $repo @{ Apply = $true; WorkerDays = 0 }
Check ($none.Code -eq 0) "workers, 0 days: exit code $($none.Code): $($none.Text)"
Check (Test-Same $allWorkers (Get-WorkerFiles $repo)) 'workers, 0 days: files changed'
$done = Invoke-Cleanup $repo @{ Apply = $true; Keep = 'kept*' }
$expected = @('busy-build', 'busy-build\sub', 'busy-build\sub\y.obj', 'busy-build\x.obj', 'kept-build', 'kept-build\z.obj', 'new.log' | Sort-Object)
Check ($done.Code -eq 0) "workers: exit code $($done.Code): $($done.Text)"
Check (Test-Same $expected (Get-WorkerFiles $repo)) "workers: left $((Get-WorkerFiles $repo) -join ', ')"
Check (Test-Path -LiteralPath (Join-Path $precious 'keep.txt')) 'workers: A LINK WAS FOLLOWED: what it leads to was deleted'
Check (Test-Same $all (Get-Files $repo)) 'workers: the artifacts changed'
Check ($done.Text -match "workers' folders") "workers: the summary does not name them: $($done.Text)"

foreach ($tree in $trees) { Remove-Item -LiteralPath $tree -Recurse -Force }
if ($failures.Count -gt 0) { throw ("harness_cleanup_test: $($failures.Count) failed`n" + ($failures -join "`n")) }
Write-Host 'harness_cleanup_test: 6 cases passed.'
