# SPDX-License-Identifier: MIT
# TG Path Tracing's in-game test run, from start to report: builds and installs the runner plugin (a developer's
# tool, in no release zip), asks it for the scenarios of tests/ingame/scenarios.json, starts the game through Steam,
# waits for the run to finish, puts the configs back, and has tools/harness/analyze.py write the report.
#   .\tools\harness\Run-PtHarness.ps1 -Inventory            lists the saves (with their pictures) and stops
#   .\tools\harness\Run-PtHarness.ps1                       every scenario whose save exists
#   .\tools\harness\Run-PtHarness.ps1 -Scenario tavern-room,npc-day  only these
#   .\tools\harness\Run-PtHarness.ps1 -Scenario refract-jar@views,mirror-tavern   one game session, a recipe per scene
#   .\tools\harness\Run-PtHarness.ps1 -Calibrate            finds the view each save was made with (tests/ingame/camera.json)
#   .\tools\harness\Run-PtHarness.ps1 -GpuCheck             only says who uses the GPU now; exit 0: an offline GPU test
#                                                           (tgpt_gpu_test) may start, up to -TestsAtOnce side by side
# The game is never started while another program uses the GPU (the owner, 2026-10-06: "check if another process is
# using the GPU, and if it is, monitor it, and launch once it settles"): the script watches the GPU, starts the game
# once it has been quiet for -QuietSeconds (5: "90 quiet seconds are too long. Just 5 quiet seconds"), and gives up
# after -GpuWaitMinutes. A file named STOP in artifacts/harness
# ends the wait. While a run lasts nothing is saved (the runner stops every save), path tracing starts Off (so the
# first frames are the game's own, never traced) and frame generation is Off (frame times are real frames).
[CmdletBinding()]
param(
    [switch]$Inventory,
    [switch]$Calibrate,
    [switch]$GpuCheck,
    # Startup/manual NR checks on either game runtime; no Mono runner or config changes.
    [switch]$LaunchOnly,
    [string[]]$Scenario = @(),
    [string]$Recipe = '',
    [string]$Plan = '',
    [string]$Targets = '',
    [string]$Out = '',
    [string]$GameDir = '',
    [int]$Frames = 0,
    [int]$TimeoutMinutes = 0,
    [int]$QuietSeconds = 5,
    [int]$GpuWaitMinutes = 240,
    [int]$TestsAtOnce = 4,
    [switch]$TestsYield,
    # Explicit owner-authorized NR comparisons (2026-10-07): GPU tests may overlap, but never another game.
    [switch]$AllowGpuContention,
    # Explicit NR/ReShade compatibility investigations; ordinary PT image comparisons keep the guard.
    [switch]$AllowReShade,
    [switch]$NoBuild,
    [switch]$NoAnalyze,
    [switch]$KeepFrames,
    [switch]$KeepHud,
    # A caller that stages a temporary installation keeps this same-process lock until it restores the files.
    [switch]$KeepLock,
    [switch]$KeepRunning,
    # FOR ONE MEASUREMENT ONLY (2026-10-10, the owner's question "Does the frametimes depend on whether the game has the
    # foreground or not?"): the game is kept MINIMIZED instead of in front, so that one run can be held against one in
    # front. Its frame rates are not the player's; the report says so.
    [switch]$KeepMinimized,
    # A run whose frame times are to be measured exactly (the owner, 2026-10-11: "Please only force the game to the
    # foreground in runs where you want to exactly measure frametimes"): the game is brought back to the front whenever
    # another window takes it. Without it the run only writes down when the game was not in front (the report says so
    # and its frame rates are not quoted), and leaves the owner's screen alone.
    [switch]$Measure,
    # FOR ONE MEASUREMENT (R19, PT-107): TG Frame Generation's "Low latency" row for this run only (Off, Reflex, ...: the
    # row's own values), where a run otherwise keeps the owner's. The configs come back after the run as always.
    [string]$LowLatency = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repo 'tools\Package.Common.ps1')
$gameProcess = 'Fall of Avalon'
$testProcess = 'tgpt_gpu_test'
# A program counts as using the GPU from this share of its 3D or compute engines (a game, a render, an AI job), or when
# it holds this much video memory (a game left paused in the background). The desktop itself stays well under both.
# The video encoder and decoder do not hold a start back (the owner, 2026-10-06: Sunshine, which streams the owner's
# screen, encodes whenever the screen changes); what they take is still written down, as it costs a little frame rate.
# Lowered 2026-10-09 (from 10 % and 2000 MB): a run began beside Starfield, which sat nearly idle at 7.5 % and
# 1,323 MB, and went on for 8 minutes while the owner played.
$busyPercent = 4.0
$busyVideoMemoryMB = 800
# The owner's other games: while one of these is OPEN no run starts, whatever it uses just then (a game at its
# menu or paused uses next to nothing and is still the owner's to play the next second). Add a name when the owner
# plays something else on this PC; the limits above catch what is not listed once it draws.
$ownerGames = @('Starfield')
function Get-OwnerGame { foreach ($name in $ownerGames) { if (Get-Process -Name $name -ErrorAction SilentlyContinue) { return $name } }; return $null }
# Windows' own processes (the kernel's display and capture work shows under System) are not another program using the
# GPU: they hold no start back and do not spoil a run's frame rates in the report; what they took is written down.
$osProcesses = @('System', 'Idle', 'dwm')
# A streaming encoder holds video memory for its frames (Sunshine: 2.4 GB at 4K, seen 2026-10-06 16:46, which held a
# run back though the owner had said to ignore the encoder). Its memory does not count; its 3D use still does.
$encoderProcesses = @('sunshine')
# The owner's editor, where this work is watched (VS Code: "Code"), holds 370 to 900 MB of video memory for its window
# at 0 % of the GPU's engines; on 2026-10-10 14:41 its 898 MB stopped a run half way. An editor is not a game: its memory
# does not count, its use of the engines still does (from 4 %, as for every program).
# NVIDIA Overlay's counter read 28,031 MB at 0 % on a 16 GB card (2026-10-30, after a recording): not real memory, and it
# held a run back for good. Same rule: its memory does not count, its use of the engines does.
$memoryNotCounted = $encoderProcesses + @('Code', 'NVIDIA Overlay')

# Every process's share of the GPU and its video memory, from Windows' own counters (one second of them).
function Get-GpuUse {
    $use = @{}; $memory = @{}
    try {
        # A process that ends between the counter's two readings makes Get-Counter report an error for the whole set
        # (seen as another game closed): the readings that are valid are still there, so errors are not fatal here.
        $set = Get-Counter -Counter '\GPU Engine(*)\Utilization Percentage' -SampleInterval 1 -MaxSamples 1 -ErrorAction SilentlyContinue
        if ($null -eq $set -or @($set.CounterSamples).Count -eq 0) { return $null }
        foreach ($sample in $set.CounterSamples) {
            if ($sample.InstanceName -match 'pid_(\d+)_.*engtype_(\w+)$') {
                $key = "$($Matches[1])|$($Matches[2])"
                $use[$key] = [double]$use[$key] + $sample.CookedValue
            }
        }
        $memorySet = Get-Counter -Counter '\GPU Process Memory(*)\Dedicated Usage' -ErrorAction SilentlyContinue
        if ($null -ne $memorySet) {
            foreach ($sample in $memorySet.CounterSamples) {
                if ($sample.InstanceName -match 'pid_(\d+)_') { $memory[$Matches[1]] = [double]$memory[$Matches[1]] + $sample.CookedValue }
            }
        }
    } catch { return $null }
    $ids = @($use.Keys | ForEach-Object { ($_ -split '\|')[0] }) + @($memory.Keys) | Sort-Object -Unique
    foreach ($id in $ids) {
        $percent = 0.0; $video = 0.0
        foreach ($key in @($use.Keys | Where-Object { $_.StartsWith("$id|") })) {
            $engine = ($key -split '\|')[1]
            if ($engine -match '^video') { if ($use[$key] -gt $video) { $video = $use[$key] } }
            elseif ($use[$key] -gt $percent) { $percent = $use[$key] }
        }
        $megabytes = [double]$memory[$id] / 1MB
        if ($megabytes -gt 64000) { $megabytes = 0 } # a counter that holds no real number (seen: 393988 MB)
        $process = Get-Process -Id ([int]$id) -ErrorAction SilentlyContinue
        $name = if ($process) { $process.ProcessName } else { "pid $id" }
        [pscustomobject]@{ Id = [int]$id; Name = $name; Percent = [Math]::Round($percent, 1); VideoPercent = [Math]::Round($video, 1); VideoMemoryMB = [int]$megabytes }
    }
}
# The programs other than the game (and the desktop's own compositor for memory) that use the GPU now: an array, empty
# when nobody does, and $null when the counters could not be read. The comma keeps an empty array an array: a
# function that returns an empty list hands its caller $null, which twice passed for "could not be read" (2026-10-06).
function Get-GpuOthers {
    $all = Get-GpuUse
    if ($null -eq $all) { return $null }
    return ,@($all | Where-Object { $_.Name -ne $gameProcess -and $osProcesses -notcontains $_.Name -and ($_.Percent -ge $busyPercent -or ($_.VideoMemoryMB -ge $busyVideoMemoryMB -and $memoryNotCounted -notcontains $_.Name)) })
}
function Format-GpuUse($rows) {
    (@($rows) | Sort-Object Percent -Descending | ForEach-Object {
        "$($_.Name) $($_.Percent) %" + $(if ($_.VideoPercent -ge 1) { " (video encoder or decoder $($_.VideoPercent) %)" } else { '' }) + ", $($_.VideoMemoryMB) MB" }) -join '; '
}
# The programs that only encode or decode video now: they hold nothing back, the report names them.
function Get-GpuVideoOnly($all) { @(@($all) | Where-Object { $_ -and $_.Name -ne $gameProcess -and $_.VideoPercent -ge 5 }) }

# The game in the foreground (the owner, 2026-10-10 15:40: "let's make sure the game always has the foreground and only
# respect our frametimes when we know it was"; narrowed 2026-10-11: "Please only force the game to the foreground in runs
# where you want to exactly measure frametimes"). Windows treats a minimized or background program otherwise (its
# priority, the driver's handling of a window nobody sees): frame rates of such a stretch are not the player's. Every two
# seconds the run looks which window has the foreground and writes down the stretches the game was not in front; with
# -Measure it also brings the game back (restore, then to the front) at once. The game takes no real input during a run
# (the plugin turns it off), so a key or a click of the owner's in that time goes nowhere.
Add-Type -Namespace TgHarness -Name Win -MemberDefinition @'
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
[DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
[DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr hWnd, bool fAltTab);
[DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
'@
# 'front', 'minimized' or 'behind <the process that has the foreground>'; empty when the game has no window yet.
function Get-GameWindowState($process) {
    if (-not $process) { return '' }
    $process.Refresh()
    $handle = $process.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) { return '' }
    if ([TgHarness.Win]::IsIconic($handle)) { return 'minimized' }
    $foreground = [TgHarness.Win]::GetForegroundWindow()
    [uint32]$owner = 0
    [void][TgHarness.Win]::GetWindowThreadProcessId($foreground, [ref]$owner)
    if ($owner -eq [uint32]$process.Id) { return 'front' }
    $who = Get-Process -Id ([int]$owner) -ErrorAction SilentlyContinue
    return "behind $(if ($who) { $who.ProcessName } else { "pid $owner" })"
}
function Set-GameInFront($process) {
    $handle = $process.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) { return }
    if ([TgHarness.Win]::IsIconic($handle)) { [void][TgHarness.Win]::ShowWindow($handle, 9) } # SW_RESTORE
    [TgHarness.Win]::SwitchToThisWindow($handle, $true)
    if (-not [TgHarness.Win]::SetForegroundWindow($handle)) {
        # Windows lets a background program take the foreground after a key press of its own (an Alt down and up).
        [TgHarness.Win]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [TgHarness.Win]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
        [void][TgHarness.Win]::SetForegroundWindow($handle)
    }
}

if ($GpuCheck) {
    $all = Get-GpuUse
    if ($null -eq $all) { Write-Host "Windows' GPU counters could not be read."; exit 2 }
    @($all | Where-Object { $_.Percent -ge 0.5 -or $_.VideoPercent -ge 0.5 -or $_.VideoMemoryMB -ge 200 }) | Sort-Object Percent -Descending | Format-Table -AutoSize | Out-Host
    $others = Get-GpuOthers
    if ($null -eq $others) { Write-Host "Windows' GPU counters could not be read."; exit 2 }
    # The tracer's offline GPU test is no "other program" to another such test. One of them keeps a single CPU thread busy
    # and the GPU at 13 to 36 % (measured 2026-10-07: 188 s of CPU in 202 s), so several run side by side at nearly the
    # speed of one. Until then the agents ran them one at a time, each waiting for a GPU with nobody on it: about 20 runs
    # of 3 to 5 minutes stood in line, and the most polite script waited 35 minutes for its first turn (the owner: "are
    # you sure they're not all just waiting on each other?"). Exit 0 here means "an offline GPU test may start", for at
    # most -TestsAtOnce of them (1: wait for a GPU with no test on it, for a run whose GPU timings count). The game is
    # another matter: a run of the game still waits until no test and no other program uses the GPU (below).
    # Another program on the GPU (the owner's other games) does not hold an offline test back either: asked "While you
    # play Starfield, may the agents' offline tests run? They never start our game, but they would take GPU from
    # Starfield", the owner chose "Yes, up to 4" (2026-10-07). -TestsYield brings the old behaviour back for one call.
    # Our own game is the exception: while it runs (a harness run of this or another session, or the owner playing it)
    # no offline test starts, since its frame rates are what is being judged. On 2026-10-07 18:03 this check said "may
    # start" beside the game at 40 % of the GPU (another session's run); only the callers' own "game closed" rule held.
    if (Get-Process -Name $gameProcess -ErrorAction SilentlyContinue) { Write-Host "The game is running: an offline GPU test waits until it has closed."; exit 1 }
    # SINCE 2026-10-12 00:20 a game of the owner's that is OPEN holds every offline test back too. That night a worker's
    # offline test hung the GPU while Starfield was open (one run of the fog check, 600 times slower than every other
    # from its first frame), Windows reset the driver (LiveKernelEvent 141, nvlddmkm 153) and Starfield was gone a
    # minute later. "Yes, up to 4" (the owner, 2026-10-07, above) was given for tests that take a share of the card,
    # not for one that can take the card down. Until the owner rules again: no offline test beside a listed game.
    $openGame = Get-OwnerGame
    if ($openGame) { Write-Host "$openGame is open (a game of the owner's): no offline GPU test starts beside it (since 2026-10-12: a test hung the card and the driver's reset took the game down). Wait until it has closed."; exit 1 }
    # A run of the game that WAITS for the card holds the lock file already (a queue's script writes it before its wait):
    # no new offline test starts then, so the run gets the next gap (docs/PT-HARNESS.md "One run at a time"; 2026-10-12
    # 02:10: two workers' chains of tests kept a run of the first priority waiting, and this check knew nothing of it).
    $waitLock = Join-Path $repo 'artifacts/harness/RUNNING'
    if (Test-Path -LiteralPath $waitLock) {
        $holder = 0
        [int]::TryParse(((Get-Content -LiteralPath $waitLock -ErrorAction SilentlyContinue | Select-Object -First 1) -split ' ')[0], [ref]$holder) | Out-Null
        if ($holder -ne 0 -and (Get-Process -Id $holder -ErrorAction SilentlyContinue)) { Write-Host "A run of the game is waiting for the card or going (process $holder, artifacts/harness/RUNNING): no offline GPU test starts until it has ended."; exit 1 }
    }
    $programs = @($others | Where-Object { $_.Name -ne $testProcess })
    if ($programs.Count -gt 0 -and $TestsYield) { Write-Host "In use: $(Format-GpuUse $programs). A run would wait."; exit 1 }
    $tests = @(Get-Process -Name $testProcess -ErrorAction SilentlyContinue).Count
    $beside = if ($programs.Count -gt 0) { " Beside it: $(Format-GpuUse $programs) (holds a run of the game back, not an offline test)." } else { '' }
    if ($tests -ge $TestsAtOnce) { Write-Host "$tests offline GPU tests are running (at most $TestsAtOnce at once): the next one waits. A run of the game would wait too.$beside"; exit 1 }
    if ($tests -gt 0) { Write-Host "$tests offline GPU tests are running: one more may start (at most $TestsAtOnce at once; GPU timings mean nothing meanwhile). A run of the game would wait for them.$beside"; exit 0 }
    if ($programs.Count -gt 0) { Write-Host "An offline GPU test may start. A run of the game would wait.$beside"; exit 0 }
    Write-Host "The GPU is free: a run would start once it has stayed free for $QuietSeconds s."
    exit 0
}

if (-not $Plan) { $Plan = Join-Path $repo 'tests\ingame\scenarios.json' }
if (-not $Targets) { $Targets = Join-Path $repo 'tests\ingame\targets.json' }
if (-not $GameDir) {
    $props = Join-Path $repo 'Build.Local.props'
    if (Test-Path -LiteralPath $props) { $GameDir = ([xml](Get-Content -LiteralPath $props -Raw)).Project.PropertyGroup.GameDirMono }
}
if (-not $GameDir -or -not (Test-Path -LiteralPath (Join-Path $GameDir 'Fall of Avalon.exe'))) { throw "No game folder. Pass -GameDir or set GameDirMono in Build.Local.props." }
if ($LaunchOnly) {
    Assert-TgGameClosed
    if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'BepInEx/core/BepInEx.dll')) -and
        -not (Test-Path -LiteralPath (Join-Path $GameDir 'BepInEx/core/BepInEx.Unity.IL2CPP.dll'))) {
        throw 'No supported BepInEx loader is installed.'
    }
    $manualRoot = Join-Path $repo 'artifacts/harness'
    $manualLock = Join-Path $manualRoot 'RUNNING'
    $manualStop = Join-Path $manualRoot 'STOP'
    if (Test-Path -LiteralPath $manualLock) {
        $holder = 0
        [int]::TryParse(((Get-Content -LiteralPath $manualLock | Select-Object -First 1) -split ' ')[0], [ref]$holder) | Out-Null
        if ($holder -ne 0 -and (Get-Process -Id $holder -ErrorAction SilentlyContinue)) {
            throw "Another harness run is active (process $holder)."
        }
    }
    New-Item -ItemType Directory -Path $manualRoot -Force | Out-Null
    Set-Content -LiteralPath $manualLock -Value "$PID manual-startup $([DateTime]::Now.ToString('s'))" -Encoding ASCII
    try {
        $waitStart = [DateTime]::Now; $quietSince = $null; $lastReport = [DateTime]::MinValue
        while ($true) {
            Assert-TgGameClosed
            if (Test-Path -LiteralPath $manualStop) { throw "Startup cancelled by $manualStop." }
            if (([DateTime]::Now - $waitStart).TotalMinutes -ge $GpuWaitMinutes) { throw 'GPU wait expired.' }
            $others = Get-GpuOthers
            if ($null -eq $others -or $others.Count -gt 0) {
                $quietSince = $null
                if (([DateTime]::Now - $lastReport).TotalSeconds -ge 30) {
                    if ($null -eq $others) { Write-Host 'Waiting: GPU counters unavailable.' }
                    else { Write-Host "Waiting: $(Format-GpuUse $others)" }
                    $lastReport = [DateTime]::Now
                }
            } else {
                if ($null -eq $quietSince) { $quietSince = [DateTime]::Now }
                if (([DateTime]::Now - $quietSince).TotalSeconds -ge $QuietSeconds) { break }
            }
            Start-Sleep -Seconds 1
        }
        Write-Host 'GPU quiet; starting the installed game for a manual startup check. No runner or config changes.'
        Start-Process 'steam://rungameid/1466060' -WindowStyle Hidden
        $launchDeadline = [DateTime]::Now.AddMinutes(2)
        do {
            Start-Sleep -Seconds 1
            $process = Get-Process -Name $gameProcess -ErrorAction SilentlyContinue
        } while (-not $process -and [DateTime]::Now -lt $launchDeadline)
        if (-not $process) { throw 'Steam did not start the game within two minutes.' }
        Write-Host "Game started (PID $($process.Id)); left running for manual validation."
    } finally {
        if ((Test-Path -LiteralPath $manualLock) -and ((Get-Content -LiteralPath $manualLock | Select-Object -First 1) -like "$PID *")) {
            Remove-Item -LiteralPath $manualLock -Force
        }
    }
    exit 0
}
Assert-TgFile (Join-Path $GameDir 'BepInEx\core\BepInEx.dll')
$run = [DateTime]::Now.ToString('yyyyMMdd-HHmmss')
$harnessRoot = Join-Path $repo 'artifacts\harness'
if (-not $Out) { $Out = Join-Path $harnessRoot $run }
$Out = [IO.Path]::GetFullPath($Out)
if (Test-Path -LiteralPath (Join-Path $Out 'request.json')) {
    throw "That output folder already contains a harness run. Use a new -Out folder so old captures cannot enter the comparison: $Out"
}
New-Item -ItemType Directory -Path $Out -Force | Out-Null
$configDir = Join-Path $GameDir 'BepInEx\config'
$configs = @('com.wessberg.tgpathtracing.cfg','com.wessberg.tgframegeneration.cfg','com.wessberg.tgbetterupscaling.cfg','com.wessberg.tgneuralrendering.cfg')
$pending = Join-Path $harnessRoot '_config-backup'
$stopFile = Join-Path $harnessRoot 'STOP'
# One run at a time (2026-10-06: a queue started a second run while one was going; the game ran one request, and the two
# runs' config backups crossed, so the player's own settings came back as the run's: path tracing and frame generation
# Off). The lock names the run's process; a lock whose process is gone is stale and taken over.
$lockFile = Join-Path $harnessRoot 'RUNNING'
if (Test-Path -LiteralPath $lockFile) {
    $holder = 0
    [int]::TryParse(((Get-Content -LiteralPath $lockFile -ErrorAction SilentlyContinue | Select-Object -First 1) -split ' ')[0], [ref]$holder) | Out-Null
    if ($holder -ne 0 -and $holder -ne $PID -and (Get-Process -Id $holder -ErrorAction SilentlyContinue)) {
        Write-Host "Another harness run is going (process $holder, $lockFile). Nothing was started."
        exit 3
    }
}
New-Item -ItemType Directory -Path $harnessRoot -Force | Out-Null
Set-Content -LiteralPath $lockFile -Value "$PID $run $([DateTime]::Now.ToString('s'))" -Encoding ASCII
function Clear-Lock {
    if (-not $KeepLock -and (Test-Path -LiteralPath $lockFile) -and ((Get-Content -LiteralPath $lockFile | Select-Object -First 1) -like "$PID *")) { Remove-Item -LiteralPath $lockFile -Force }
}

function Restore-Configs {
    if (-not (Test-Path -LiteralPath $pending)) { return }
    foreach ($name in $configs) {
        $saved = Join-Path $pending $name
        if (Test-Path -LiteralPath $saved) { Copy-Item -LiteralPath $saved -Destination (Join-Path $configDir $name) -Force }
    }
    Remove-Item -LiteralPath $pending -Recurse -Force
}
# One "Key = Value" line of a BepInEx config, in its section; the file's encoding and line ends stay.
function Set-ConfigValue([string]$Path, [string]$Section, [string]$Key, [string]$Value) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $text = [IO.File]::ReadAllText($Path)
    $lines = $text -split "`r?`n"
    $current = ''; $done = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line -match '^\[(.+)\]\s*$') { $current = $Matches[1]; continue }
        if ($current -eq $Section -and $line.StartsWith("$Key = ")) { $lines[$i] = "$Key = $Value"; $done = $true; break }
    }
    if ($done) {
        $ending = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
        [IO.File]::WriteAllText($Path, ($lines -join $ending), (New-Object Text.UTF8Encoding $false))
    }
    $done
}
function Get-SaveListing {
    $dirs = @(Get-ChildItem -LiteralPath (Join-Path $env:USERPROFILE 'AppData\LocalLow\Questline\Fall of Avalon') -Directory -ErrorAction SilentlyContinue |
              ForEach-Object { Join-Path $_.FullName 'Saved' } | Where-Object { Test-Path -LiteralPath $_ })
    $rows = foreach ($dir in $dirs) {
        Get-ChildItem -LiteralPath $dir -Filter '*.zip' -File | ForEach-Object { "{0}|{1}|{2}" -f $_.FullName, $_.Length, $_.LastWriteTimeUtc.Ticks }
    }
    @($rows | Sort-Object)
}

# The request: each scenario's steps come from its recipe.
$planData = Get-Content -LiteralPath $Plan -Raw | ConvertFrom-Json
$cameraFile = Join-Path (Split-Path -Parent $Plan) 'camera.json'
$views = $null
if (Test-Path -LiteralPath $cameraFile) { $views = (Get-Content -LiteralPath $cameraFile -Raw | ConvertFrom-Json).views }
# A scenario may name its recipe for this run: "refract-jar@views,mirror-tavern" runs both in one game session, the jar
# with the views recipe, the mirror with its own (each game start costs about a minute, 2026-10-07).
$wantedRecipe = @{}
$wanted = @($Scenario | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ } | ForEach-Object {
    $parts = $_ -split '@', 2
    if ($parts.Count -eq 2 -and $parts[1]) { $wantedRecipe[$parts[0]] = $parts[1] }
    $parts[0]
})
$scenarios = @()
foreach ($key in $wanted) {
    if (@($planData.scenarios.key) -notcontains $key) { throw "Unknown scenario '$key'. Available: $($planData.scenarios.key -join ', ')." }
}
if (-not $Inventory) {
    foreach ($s in @($planData.scenarios)) {
        if ($wanted.Count -gt 0 -and $wanted -notcontains $s.key) { continue }
        $names = @($s.PSObject.Properties.Name)
        if ($wanted.Count -eq 0 -and $names -contains 'skip' -and $s.skip) { continue }
        $recipeName = if ($wantedRecipe.ContainsKey($s.key)) { $wantedRecipe[$s.key] } elseif ($Recipe) { $Recipe } elseif ($names -contains 'recipe') { $s.recipe } else { $planData.defaults.recipe }
        if (@($planData.recipes.PSObject.Properties.Name) -notcontains $recipeName) { throw "Scenario $($s.key) names the recipe '$recipeName', which the plan does not have." }
        $steps = @()
        foreach ($step in @(if ($Calibrate) { @() } else { $planData.recipes.$recipeName })) {
            $copy = [ordered]@{}
            foreach ($p in $step.PSObject.Properties) { $copy[$p.Name] = $p.Value }
            if ($Frames -gt 0 -and $copy.Contains('frames') -and $copy['frames'] -gt 0) { $copy['frames'] = $Frames }
            if ($copy.Contains('set')) { $copy['set'] = @($copy['set']) }
            $steps += $copy
        }
        $entry = [ordered]@{ key = $s.key; save = $s.save; steps = $steps }
        foreach ($field in 'saveFallback','settleAfterLoad','hour','minute','pitch','yawOffset','freeze') { if ($names -contains $field) { $entry[$field] = $s.$field } }
        if ($names -contains 'missingBoxes') { $entry['missingBoxes'] = @($s.missingBoxes) }
        if ($names -contains 'mirror') { $entry['mirror'] = @($s.mirror) }
        if ($names -contains 'lantern') { $entry['lantern'] = @($s.lantern) }
        # The view the save was made with, where a calibration found it (a save comes back looking level); the runner
        # uses it only for the very save it was measured on.
        if (-not $Calibrate -and $names -notcontains 'pitch' -and $views -and @($views.PSObject.Properties.Name) -contains $s.key) {
            $view = $views.($s.key)
            $entry['pitch'] = $view.pitch; $entry['yawOffset'] = $view.yawOffset; $entry['cameraSaved'] = $view.saved
        }
        $scenarios += $entry
    }
    if ($scenarios.Count -eq 0) { throw 'No scenario matches. See tests/ingame/scenarios.json.' }
}
$request = [ordered]@{
    run = $run; outDir = $Out; mode = $(if ($Inventory) { 'inventory' } elseif ($Calibrate) { 'calibrate' } else { 'scenarios' }); quit = (-not $KeepRunning)
    scenarios = $scenarios
    hideHud = (-not $KeepHud)
}
$requestDir = Join-Path $GameDir 'BepInEx\TGPathTracing\Harness'
$requestJson = ConvertTo-Json -InputObject $request -Depth 10
[IO.File]::WriteAllText((Join-Path $Out 'request.json'), $requestJson, (New-Object Text.UTF8Encoding $false))

# The GPU must be free, and stay free for a while, before the game starts: another game's frames would be slowed, and
# this run's frame times would mean nothing. Nothing is written into the game folder before that: a game the owner
# starts meanwhile plays as normal.
$gpuLog = [ordered]@{ waitedSeconds = 0; before = ''; during = @{}; video = @{}; samples = 0; readable = $true; windowChecks = 0; notInFront = @() }
$waitStart = [DateTime]::Now; $quietSince = $null; $lastSaid = [DateTime]::MinValue
while ($true) {
    if (Test-Path -LiteralPath $stopFile) { Write-Host "Stopped by $stopFile. Nothing was started."; Clear-Lock; exit 3 }
    $others = Get-GpuOthers
    $playing = Get-Process -Name $gameProcess -ErrorAction SilentlyContinue
    $starfield = Get-OwnerGame
    if ($AllowGpuContention -and -not $playing -and -not $starfield) {
        Write-Host 'Owner-authorized concurrent GPU work: Tainted Grail and Starfield are closed. Other GPU use will be recorded; timings may be affected.'
        break
    }
    # A reading that failed is not a free GPU.
    $unknown = $null -eq $others
    # An offline test that is still making its passes uses the CPU alone and does not show as GPU use: its process counts
    # (2026-10-12 01:19: a run began beside a worker's test in its first minutes, and the test had to be stopped by hand).
    $testing = @(Get-Process -Name $testProcess -ErrorAction SilentlyContinue).Count
    if (-not $unknown -and $others.Count -eq 0 -and -not $playing -and -not $starfield -and $testing -eq 0) {
        if (-not $quietSince) { $quietSince = [DateTime]::Now }
        if (([DateTime]::Now - $quietSince).TotalSeconds -ge $QuietSeconds) { break }
    } else {
        $quietSince = $null
        if (([DateTime]::Now - $lastSaid).TotalSeconds -ge 60) {
            $what = if ($playing) { 'the game itself is running (close it for a run)' } elseif ($starfield) { "$starfield is open (a game of the owner's: no run beside it, whatever it uses)" } elseif ($testing -gt 0 -and -not $unknown -and $others.Count -eq 0) { "$testing offline GPU test(s) running (making passes: no GPU use yet)" } elseif ($unknown) { "Windows' GPU counters could not be read, so it is not known to be free" } else { Format-GpuUse $others }
            Write-Host ("{0} The GPU is in use: {1}. Waiting until it has been free for {2} s." -f [DateTime]::Now.ToString('HH:mm:ss'), $what, $QuietSeconds)
            $lastSaid = [DateTime]::Now
        }
    }
    if (([DateTime]::Now - $waitStart).TotalMinutes -ge $GpuWaitMinutes) { Write-Host "The GPU was not free within $GpuWaitMinutes minutes. Nothing was started."; exit 3 }
    Start-Sleep -Seconds 4
}
$gpuLog.waitedSeconds = [int]([DateTime]::Now - $waitStart).TotalSeconds
$gpuLog.before = Format-GpuUse (@(Get-GpuUse) | Where-Object { $_ -and ($_.Percent -ge 1 -or $_.VideoMemoryMB -ge 500) })
Assert-TgGameClosed
if (Get-OwnerGame) { throw "$(Get-OwnerGame) started while preparing the run. Nothing was started." }

# A run that was cut short left the player's configs changed: put them back before anything else.
Restore-Configs
# The runner plugin: built from src/TGPathTracing.Harness and copied beside the mods.
$project = Join-Path $repo 'src\TGPathTracing.Harness\TGPathTracing.Harness.csproj'
$built = Join-Path $repo 'src\TGPathTracing.Harness\bin\Release\net472\TGPathTracing.Harness.dll'
if (-not $NoBuild) {
    & dotnet build $project -c Release -f net472 -p:TargetFrameworks=net472 -p:DeployToGame=false --nologo -v q | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'The runner plugin did not build.' }
}
Assert-TgFile $built
$installed = Join-Path $GameDir 'BepInEx\plugins\TGPathTracingHarness\TGPathTracing.Harness.dll'
Copy-TgReplacing $built $installed
if ((Get-FileHash -LiteralPath $built -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash) { throw 'The installed runner plugin differs from the build.' }

# The player's configs, kept aside and put back when the game has closed. Path tracing starts Off, so a scenario's
# first frames are the game's own with the tracer never on; frame generation is Off for the run.
New-Item -ItemType Directory -Path $pending -Force | Out-Null
foreach ($name in $configs) {
    $path = Join-Path $configDir $name
    if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination (Join-Path $pending $name) -Force }
}
$savesBefore = Get-SaveListing
# Which build of the mod this run tests: the installed files' hashes, beside its results.
$tested = [ordered]@{}
foreach ($file in @('BepInEx\plugins\TGPathTracing\TGPathTracing.dll','BepInEx\TGPathTracing\Native\TGPathTracing.Native.dll','BepInEx\plugins\TGBetterUpscaling\TGBetterUpscaling.dll','BepInEx\TGBetterUpscaling\Native\TGBetterUpscaling.Native.dll',
    'BepInEx\plugins\TGNeuralRendering\TGNeuralRendering.dll','BepInEx\TGNeuralRendering\Native\TGNeuralRendering.Native.dll','BepInEx\TGNeuralRendering\Native\TGGraphicsBridge.Native.dll','BepInEx\TGNeuralRendering\Native\DLSS\nvngx_dlssnr.dll')) {
    $path = Join-Path $GameDir $file
    if (Test-Path -LiteralPath $path) { $tested[(Split-Path -Leaf $file)] = [ordered]@{ sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash; written = (Get-Item -LiteralPath $path).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss') } }
}
ConvertTo-Json -InputObject $tested -Depth 4 | Set-Content -LiteralPath (Join-Path $Out 'build.json') -Encoding UTF8
$exit = 1
# Native shader/dispatch/draw hooks must be present before Unity creates shaders, including with PT Off.
# Restore the marker's previous bytes in finally; the native observer has no normal-play hooks.
$vfxMarker = Join-Path $requestDir 'vfx-observer.enable'
$vfxCapture = @($request.scenarios | ForEach-Object { $_.steps } | Where-Object { $_.Contains('vfxCapture') -and $_['vfxCapture'] }).Count -gt 0
$vfxSimulation = @($request.scenarios | ForEach-Object { $_.steps } | Where-Object { $_.Contains('vfxSimulation') -and $_['vfxSimulation'] }).Count -gt 0
$vfxPrevious = if (Test-Path -LiteralPath $vfxMarker) { [IO.File]::ReadAllBytes($vfxMarker) } else { $null }
try {
    [void](Set-ConfigValue (Join-Path $configDir 'com.wessberg.tgpathtracing.cfg') 'Path Tracing' 'Mode' 'Off')
    [void](Set-ConfigValue (Join-Path $configDir 'com.wessberg.tgpathtracing.cfg') 'Path Tracing' 'Debug view' 'None')
    [void](Set-ConfigValue (Join-Path $configDir 'com.wessberg.tgframegeneration.cfg') 'Video' 'Frame generation' 'Off')
    if ($LowLatency) {
        [void](Set-ConfigValue (Join-Path $configDir 'com.wessberg.tgframegeneration.cfg') 'Video' 'Low latency' $LowLatency)
        Write-Host "Low latency for this run: $LowLatency (TG Frame Generation's row; the owner's comes back after the run)."
    }
    # Ordinary PT recipes must stay independent of the player's saved NR choice. NR comparison
    # recipes enable it explicitly per take. Restore both values from the existing config backup.
    [void](Set-ConfigValue (Join-Path $configDir 'com.wessberg.tgneuralrendering.cfg') 'Video' 'Neural Rendering' 'Off')
    [void](Set-ConfigValue (Join-Path $configDir 'com.wessberg.tgneuralrendering.cfg') 'Internal' 'Preferred neural rendering' 'Off')
    New-Item -ItemType Directory -Path $requestDir -Force | Out-Null
    if ($vfxSimulation) {
        [void](Set-ConfigValue (Join-Path $configDir 'com.wessberg.tgpathtracing.cfg') 'Advanced' 'Particle simulation diagnostic' 'true')
        [IO.File]::WriteAllText($vfxMarker, 'simulation-buffers')
    } elseif ($vfxCapture) { [IO.File]::WriteAllText($vfxMarker, 'Diagnostic dispatch/draw observer; metadata only; enabled for this harness run.') }
    [IO.File]::WriteAllText((Join-Path $requestDir 'request.json'), $requestJson, (New-Object Text.UTF8Encoding $false))
    if ($TimeoutMinutes -le 0) { $TimeoutMinutes = if ($Inventory) { 8 } else { 10 + 6 * $scenarios.Count } }
    Assert-TgGameClosed
    # ReShade (the owner's, with RenoDX) changes the game's tone mapping and its swapchain: a run with it on judges another
    # picture, and on 2026-10-08 12:42 its steps saved no frames at all. The owner turns it on and off (it loads from
    # dxgi.dll in the game folder); the harness never touches it. Only explicitly marked NR compatibility
    # recipes may opt in; ordinary PT runs still refuse it.
    $reshade = @('dxgi.dll', 'd3d11.dll', 'ReShade64.dll') | Where-Object { Test-Path -LiteralPath (Join-Path $GameDir $_) } |
        Where-Object { "$((Get-Item -LiteralPath (Join-Path $GameDir $_)).VersionInfo.ProductName)$((Get-Item -LiteralPath (Join-Path $GameDir $_)).VersionInfo.FileDescription)" -match 'ReShade' }
    if ($AllowReShade -and @($request.scenarios | ForEach-Object { $_.steps } | Where-Object { $_.Contains('neural') }).Count -eq 0) {
        throw '-AllowReShade requires an explicit Neural Rendering comparison recipe. Ordinary PT runs require ReShade off.'
    }
    if ($reshade -and -not $AllowReShade) { throw "ReShade is on in the game folder ($($reshade -join ', ')): its tone mapping changes the picture a run judges. Turn it off, or use -AllowReShade for an explicit compatibility investigation." }
    if ($reshade) { Write-Host "ReShade compatibility run: $($reshade -join ', '). Captures include its active configuration; this is not a plain PT baseline." }
    if (Get-OwnerGame) { throw "$(Get-OwnerGame) started while preparing the run. Nothing was started." }
    $gate = if ($AllowGpuContention) { 'Tainted Grail and Starfield are closed; GPU contention permitted for this run' } else { "The GPU has been free for $QuietSeconds s" }
    Write-Host "Run $run -> $Out ($($scenarios.Count) scenarios, at most $TimeoutMinutes minutes). ${gate}: starting the game through Steam."
    Start-Process 'steam://rungameid/1466060'
    $deadline = [DateTime]::Now.AddMinutes($TimeoutMinutes)
    $seen = 0; $started = [DateTime]::Now; $tick = 0; $intruded = 0; $behindSince = $null; $behindWhat = ''
    $done = Join-Path $Out 'done.json'; $progress = Join-Path $Out 'progress.log'
    # The tracer's own word that the GPU stopped (a driver reset) ends the run: the game stays open with a dead tracer and
    # the step would stand until the run's time is up (2026-10-12: ten minutes on a hung walk). The log runs on across
    # starts, so it is read from where it stood before this start.
    $nativeLog = Join-Path $GameDir 'BepInEx\TGPathTracing\Native\TGPathTracing.Native.log'
    $nativeLogAt = if (Test-Path -LiteralPath $nativeLog) { (Get-Item -LiteralPath $nativeLog).Length } else { 0 }
    $nativeTail = ''; $gpuStopped = $false
    while ($true) {
        Start-Sleep -Seconds 2
        $process = Get-Process -Name $gameProcess -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $progress) {
            $lines = @(Get-Content -LiteralPath $progress)
            if ($lines.Count -gt $seen) { $lines[$seen..($lines.Count - 1)] | ForEach-Object { Write-Host "  $_" }; $seen = $lines.Count }
        }
        if (Test-Path -LiteralPath $done) { break }
        if (-not $process -and ([DateTime]::Now - $started).TotalSeconds -gt 120) { Write-Warning 'The game is not running and the run has not finished.'; break }
        if ([DateTime]::Now -gt $deadline) { Write-Warning "The run did not finish in $TimeoutMinutes minutes."; break }
        if (Test-Path -LiteralPath $stopFile) { Write-Warning "Stopped by $stopFile."; break }
        if (($tick % 5) -eq 0 -and (Test-Path -LiteralPath $nativeLog)) {
            try {
                $fs = [IO.File]::Open($nativeLog, 'Open', 'Read', 'ReadWrite, Delete')
                try {
                    if ($fs.Length -lt $nativeLogAt) { $nativeLogAt = 0 }
                    if ($fs.Length -gt $nativeLogAt) {
                        [void]$fs.Seek($nativeLogAt, 'Begin')
                        $buf = New-Object byte[] ([int][Math]::Min($fs.Length - $nativeLogAt, 8MB)); $read = $fs.Read($buf, 0, $buf.Length)
                        $nativeLogAt += $read; $text = $nativeTail + [Text.Encoding]::UTF8.GetString($buf, 0, $read)
                        $nativeTail = if ($text.Length -gt 64) { $text.Substring($text.Length - 64) } else { $text }
                        if ($text.Contains('The GPU stopped')) { $gpuStopped = $true }
                    }
                } finally { $fs.Dispose() }
            } catch { }
            if ($gpuStopped) { Write-Warning "The tracer's log says the GPU stopped (a driver reset): the run ends here. Read the line in TGPathTracing.Native.log."; break }
        }
        # The game in front: written down as stretches (from, to, what had the foreground) and brought back at once.
        $state = Get-GameWindowState $process
        if ($state -and $KeepMinimized) {
            $gpuLog.windowChecks++; $gpuLog.keptMinimized = $true
            if ($state -ne 'minimized') { try { [void][TgHarness.Win]::ShowWindow($process.MainWindowHandle, 6) } catch { } } # SW_MINIMIZE
        } elseif ($state) {
            $gpuLog.windowChecks++
            $now = [DateTime]::Now.ToString('HH:mm:ss.fff')
            if ($state -ne 'front') {
                if ($null -eq $behindSince) { $behindSince = $now; $behindWhat = $state; Write-Host "  $now the game is not in front ($state)$(if ($Measure) { ': bringing it back' } else { ': left so (no -Measure), its frame rates are not quoted' })." }
                if ($Measure) { try { Set-GameInFront $process } catch { } }
            } elseif ($null -ne $behindSince) {
                $gpuLog.notInFront += "$behindSince to $now ($behindWhat)"; $behindSince = $null
            }
        }
        # Who else used the GPU while the run lasted: its frame times are only worth something if nobody did.
        if ((++$tick % 5) -eq 0) {
            $all = Get-GpuUse
            if ($null -eq $all) { continue }
            $gpuLog.samples++
            foreach ($other in @($all | Where-Object { $_.Name -ne $gameProcess -and $osProcesses -notcontains $_.Name -and ($_.Percent -ge $busyPercent -or ($_.VideoMemoryMB -ge $busyVideoMemoryMB -and $memoryNotCounted -notcontains $_.Name)) })) {
                if (-not $gpuLog.during.ContainsKey($other.Name) -or $gpuLog.during[$other.Name] -lt $other.Percent) { $gpuLog.during[$other.Name] = $other.Percent }
            }
            foreach ($other in @($all | Where-Object { $osProcesses -contains $_.Name -and $_.Percent -ge $busyPercent })) {
                $label = "$($other.Name) (Windows itself)"
                if (-not $gpuLog.video.ContainsKey($label) -or $gpuLog.video[$label] -lt $other.Percent) { $gpuLog.video[$label] = $other.Percent }
            }
            foreach ($other in (Get-GpuVideoOnly $all)) {
                if (-not $gpuLog.video.ContainsKey($other.Name) -or $gpuLog.video[$other.Name] -lt $other.VideoPercent) { $gpuLog.video[$other.Name] = $other.VideoPercent }
            }
            # Another program of the owner's takes the card while the run goes (the owner started to play): the run
            # ends at once and the game closes, so that it has the card (the owner, 2026-10-06: never beside another
            # game or program). The workers' offline tests are not such a program. Two readings in a row (20 s), or a
            # listed game open at all.
            # The streaming encoder alone never ends a run (2026-10-11): Sunshine copies and converts every frame our
            # own game shows, 4 to 5 % of the 3D engines while the game runs, and the owner: "Sunshine practically always
            # runs". Twice in a row over the limit it ended a look-only run of the forest (20261011-223336). What it took
            # is still written down above, and the report still says another program used the GPU (frame rates of
            # such a run are not quoted: AGENTS.md; the owner's ruling on that is open).
            $intruders = @($all | Where-Object { $_.Name -ne $gameProcess -and $_.Name -ne $testProcess -and $osProcesses -notcontains $_.Name -and $encoderProcesses -notcontains $_.Name -and ($_.Percent -ge $busyPercent -or ($_.VideoMemoryMB -ge $busyVideoMemoryMB -and $memoryNotCounted -notcontains $_.Name)) })
            $intruded = if ($intruders.Count) { $intruded + 1 } else { 0 }
            $game = Get-OwnerGame
            if ($game -or $intruded -ge 2) {
                $who = if ($game) { "$game is open" } else { ($intruders | ForEach-Object { "$($_.Name) $($_.Percent) %, $($_.VideoMemoryMB) MB" }) -join '; ' }
                Write-Warning "Another program has the GPU ($who): the run is stopped and the game closed."
                $gpuLog.stoppedFor = $who
                Get-Process -Name $gameProcess -ErrorAction SilentlyContinue | Stop-Process -Force
                break
            }
        }
    }
    if (-not $KeepRunning) {
        for ($i = 0; $i -lt 30 -and (Get-Process -Name $gameProcess -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Seconds 1 }
        $left = Get-Process -Name $gameProcess -ErrorAction SilentlyContinue
        if ($left) {
            Write-Warning 'The game did not close by itself: stopping it.'; $left | Stop-Process -Force
            # A game whose GPU was reset takes longer than 3 s to go (2026-10-12: it still stood, and the owner's configs
            # stayed in the backup folder "for the next run"): wait for it, so that they come back now.
            for ($i = 0; $i -lt 30 -and (Get-Process -Name $gameProcess -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Seconds 1 }
        }
    }
    if (Test-Path -LiteralPath $done) { $exit = 0 }
} finally {
    if ($vfxCapture) {
        if ($null -ne $vfxPrevious) { [IO.File]::WriteAllBytes($vfxMarker, $vfxPrevious) }
        elseif (Test-Path -LiteralPath $vfxMarker) { Remove-Item -LiteralPath $vfxMarker -Force }
    }
    $leftover = Join-Path $requestDir 'request.json'
    if (Test-Path -LiteralPath $leftover) { Remove-Item -LiteralPath $leftover -Force }
    if (-not (Get-Process -Name $gameProcess -ErrorAction SilentlyContinue)) { Restore-Configs }
    else { Write-Warning "The game still runs: its configs are put back by the next run (kept in $pending)." }
}
if ((Test-Path variable:behindSince) -and $null -ne $behindSince) { $gpuLog.notInFront += "$behindSince to the run's end ($behindWhat)" }
ConvertTo-Json -InputObject $gpuLog -Depth 4 | Set-Content -LiteralPath (Join-Path $Out 'gpu.json') -Encoding UTF8
if ($gpuLog.windowChecks -eq 0) { Write-Warning 'The game''s window was never seen: whether it was in front is not known, and its frame rates are not to be trusted.' }
elseif (@($gpuLog.notInFront).Count -gt 0) { Write-Warning ("The game was not in front during the run: " + (@($gpuLog.notInFront) -join '; ') + ". Frame rates of steps in those stretches are not to be trusted.") }
if ($gpuLog.during.Count -gt 0) { Write-Warning ("Other programs used the GPU during the run: " + (($gpuLog.during.Keys | ForEach-Object { "$_ up to $($gpuLog.during[$_]) %" }) -join '; ')) }
# Nothing may have been saved: the save files are the same files, byte counts and times as before.
$savesAfter = Get-SaveListing
$changed = @(Compare-Object -ReferenceObject $savesBefore -DifferenceObject $savesAfter)
[ordered]@{ files = $savesAfter.Count; untouched = ($changed.Count -eq 0); changes = @($changed | ForEach-Object { "$($_.SideIndicator) $($_.InputObject)" }) } |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Out 'saves-check.json') -Encoding UTF8
Write-Host ("Save files: {0}, {1}." -f $savesAfter.Count, $(if ($changed.Count -eq 0) { 'untouched by the run' } else { "CHANGED ($($changed.Count) differences, saves-check.json)" }))
# The logs of this start, beside the results.
foreach ($log in @('BepInEx\LogOutput.log','BepInEx\TGPathTracing\Native\TGPathTracing.Native.log','BepInEx\TGBetterUpscaling\Native\TGBetterUpscaling.Native.log','BepInEx\TGGraphicsBridge\TGGraphicsBridge.Native.log')) {
    $source = Join-Path $GameDir $log
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $Out (Split-Path -Leaf $log)) -Force }
}
if ($Calibrate) {
    & python (Join-Path $PSScriptRoot 'calibrate.py') $Out --camera $cameraFile
    if ($LASTEXITCODE -ne 0) { $exit = [Math]::Max($exit, $LASTEXITCODE) }
} elseif (-not $NoAnalyze -and -not $Inventory) {
    $analysisSteps = @($request.scenarios | ForEach-Object { $_.steps })
    if ($vfxCapture) {
        if ($vfxSimulation) { & python (Join-Path $PSScriptRoot 'vfx_simulation.py') $Out }
        else { & python (Join-Path $PSScriptRoot 'vfx_capture.py') $Out }
        if ($LASTEXITCODE -ne 0) { $exit = [Math]::Max($exit, $LASTEXITCODE) }
    }
    if (@($analysisSteps | Where-Object { $_.Contains('effects') -and $_['effects'] }).Count -gt 0) {
        & python (Join-Path $PSScriptRoot 'effects.py') $Out
        if ($LASTEXITCODE -ne 0) { $exit = [Math]::Max($exit, $LASTEXITCODE) }
    }
    if (@($analysisSteps | Where-Object { $_.Contains('missing') -and $_['missing'] }).Count -gt 0) {
        & python (Join-Path $PSScriptRoot 'missing.py') $Out
        if ($LASTEXITCODE -ne 0) { $exit = [Math]::Max($exit, $LASTEXITCODE) }
    }
    # The effects/missing recipes take no image sequences. Mixed runs still get the usual image analysis.
    if (@($analysisSteps | Where-Object { -not $_.Contains('frames') -or $_['frames'] -gt 0 }).Count -gt 0) {
        $arguments = @((Join-Path $PSScriptRoot 'analyze.py'), $Out, '--plan', $Plan, '--targets', $Targets)
        if ($KeepFrames) { $arguments += '--keep-frames' }
        & python @arguments
        if ($LASTEXITCODE -ne 0) { $exit = [Math]::Max($exit, $LASTEXITCODE) }
    }
}
Write-Host "Results: $Out"
Clear-Lock
exit $exit
