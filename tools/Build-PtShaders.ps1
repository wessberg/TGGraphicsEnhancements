# SPDX-License-Identifier: MIT
# TG Path Tracing's shader passes compiled side by side, before the NMake build that would compile them one at a time.
#
#   .\tools\Build-PtShaders.ps1 [-BuildDir build\native] [-Throttle 14] [-MachineLimit 6] [-FreeGb 16] [-PeakGb 8] [-First]
#
# Why: an NMake tree runs one command at a time, and the tracer's passes have grown (2026-10-08: pt_specular alone took
# over 12 minutes of one core, the whole native build about an hour on a 32-thread PC). This reads the very commands
# CMake wrote for the shader headers (CMakeFiles\TGPathTracingShaders.dir\build.make: one dxc line a header, with the
# files it depends on), runs those whose header is missing or older than a file it depends on, several at a time, and
# leaves the rest to `cmake --build`, which then finds the headers up to date and only compiles C++ and links.
# Nothing here knows a flag or a pass by name: when CMakeLists.txt changes them, configure again and this follows.
# A failed compile prints its errors, its half-written header is removed, and the exit code is 1.
#
# One PC, several trees. -Throttle is this run's own limit; -MachineLimit and -FreeGb hold for the whole PC, whoever
# started the other compiles (2026-10-09: five workers compiled at once, 16 dxc held 35 GB and three GPU tests 12 GB of
# 62; the game then could not create its Direct3D 12 device, 0x8007000E, and the bridge fell back to Windows' own
# runtime: no DirectX Raytracing 1.2, a frame of 2 s). A compile starts only while fewer than -MachineLimit dxc run on
# the PC and -FreeGb of memory are free (a large pass takes up to 6 GB); while the game runs, a third as many and twice
# the memory; the same while a game run waits to start (the file %TEMP%\tgb\GAME-WANTED, which the coordinator's run
# helper keeps while it waits and runs; older than three hours it is taken for left behind). One compile always may
# run when no other does, so nothing waits for ever.
# The free memory is counted LESS what the compiles already running will still take: a big pass (the reflections,
# the bounced light, the walk) starts small and grows to -PeakGb over minutes, so every running dxc, whoever
# started it, holds back the rest of its peak (2026-10-09 14:55: twelve dxc all passed the test at their start and
# then committed 100 GB on this 62 GB PC, which paged for minutes with the owner at it). A small pass ends within
# seconds and gives its share back. Never raise -MachineLimit or lower -FreeGb while other trees compile.
# -First is for the coordinator's build of the MAIN tree alone (the build the next look at the game waits for): while
# it runs it keeps the file %TEMP%\tgb\BUILD-FIRST fresh, and every other run of this script starts no new compile
# until that file is gone or a minute old (compiles already running end by themselves). A -First run is not held by
# -MachineLimit, only by the memory. A worker never passes -First.
param(
    [string]$BuildDir = 'build\native',
    [string]$MachineTemp = $env:TEMP, # shared coordinator markers when a worker isolates compiler TEMP

    [int]$Throttle = [Math]::Max(2, [Math]::Min(14, [int]([Environment]::ProcessorCount / 2))),
    [int]$MachineLimit = 6,
    [int]$FreeGb = 16,
    [double]$PeakGb = 8,
    [switch]$First
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $repo $BuildDir }
$make = Join-Path $BuildDir 'CMakeFiles\TGPathTracingShaders.dir\build.make'
if (-not (Test-Path -LiteralPath $make)) { Write-Host "No $make : configure the tree first."; exit 2 }
# The list of commands is CMake's: when CMakeLists.txt is newer than it (a merge added or removed a pass), have CMake
# write it again first, as `cmake --build` would (2026-10-08: the water job replaced two passes, and the old list
# still named their files).
$cache = Join-Path $BuildDir 'CMakeCache.txt'
$source = (Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' | Select-Object -First 1).Matches.Groups[1].Value
$lists = if ($source) { Join-Path ($source -replace '/', '\') 'CMakeLists.txt' } else { '' }
if ($lists -and (Test-Path -LiteralPath $lists) -and (Get-Item -LiteralPath $lists).LastWriteTimeUtc -gt (Get-Item -LiteralPath $make).LastWriteTimeUtc) {
    Write-Host "$(Get-Date -Format HH:mm:ss) CMakeLists.txt is newer than the build's list of passes: configuring again."
    $vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    & cmd /c "`"$vcvars`" >nul && cmake `"$BuildDir`"" | Select-Object -Last 3 | Out-Host
    if ($LASTEXITCODE -ne 0) { Write-Host "Configuring failed ($LASTEXITCODE)."; exit 2 }
}

$dependencies = @{}
$jobs = New-Object System.Collections.Generic.List[object]
foreach ($line in [IO.File]::ReadAllLines($make)) {
    if ($line -match '^(generated\\pathtracing\\[^:\s]+\.h): (.+)$') {
        $key = (Split-Path $Matches[1] -Leaf)
        if (-not $dependencies.ContainsKey($key)) { $dependencies[$key] = New-Object System.Collections.Generic.List[string] }
        $dependencies[$key].Add($Matches[2].Trim())
    } elseif ($line -match "^\t(\S*dxc\.exe)\s+(.*-Fh\s+(\S+).*)$") {
        $jobs.Add([pscustomobject]@{ Exe = $Matches[1]; Arguments = $Matches[2]; Output = ($Matches[3] -replace '/', '\') })
    }
}
if ($jobs.Count -eq 0) { Write-Host "No dxc command found in $make."; exit 2 }

$stale = New-Object System.Collections.Generic.List[object]
foreach ($job in $jobs) {
    $name = Split-Path $job.Output -Leaf
    $built = Get-Item -LiteralPath $job.Output -ErrorAction SilentlyContinue
    $newest = [datetime]::MinValue
    foreach ($d in $dependencies[$name]) {
        $f = Get-Item -LiteralPath $d -ErrorAction SilentlyContinue
        if (-not $f) { $newest = [datetime]::MaxValue; break }   # A file it depends on is gone or not made yet: compile.
        if ($f.LastWriteTimeUtc -gt $newest) { $newest = $f.LastWriteTimeUtc }
    }
    if (-not $built -or $built.Length -eq 0 -or $built.LastWriteTimeUtc -le $newest) { $stale.Add($job) }
}
Write-Host "$(Get-Date -Format HH:mm:ss) shader headers: $($jobs.Count) in all, $($stale.Count) to compile, $Throttle at a time."
if ($stale.Count -eq 0) { exit 0 }

$logs = Join-Path $BuildDir 'shader-logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
# The largest sources first: the longest compile decides when the whole thing ends.
$queue = New-Object System.Collections.Generic.Queue[object]
foreach ($job in ($stale | Sort-Object { $n = Split-Path $_.Output -Leaf; if ($n -match 'specular|water|gi(_|\.)|prepare|walk') { 0 } else { 1 } })) { $queue.Enqueue($job) }
$running = New-Object System.Collections.ArrayList
$failed = New-Object System.Collections.Generic.List[string]
$watch = [Diagnostics.Stopwatch]::StartNew()
# Whether the PC has room for one more compile (the header's note): by every dxc on it and its free memory.
function Test-Room {
    $compiles = @(Get-Process -Name dxc -ErrorAction SilentlyContinue)
    $all = $compiles.Count
    if ($all -eq 0) { return $true }
    $game = [bool](Get-Process -Name 'Fall of Avalon' -ErrorAction SilentlyContinue)
    if (-not $game) {
        $wanted = Get-Item -LiteralPath (Join-Path $MachineTemp 'tgb\GAME-WANTED') -ErrorAction SilentlyContinue
        $game = $wanted -and $wanted.LastWriteTime -gt (Get-Date).AddHours(-3)
    }
    if (-not $First -and $all -ge $(if ($game) { [Math]::Max(2, [int]($MachineLimit / 3)) } else { $MachineLimit })) { return $false }
    $free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB
    # What the running compiles have not taken yet of their peak.
    $owed = 0.0
    foreach ($c in $compiles) { try { $owed += [Math]::Max(0.0, $PeakGb - $c.WorkingSet64 / 1GB) } catch { } }
    return ($free - $owed) -ge $(if ($game) { 2 * $FreeGb } else { $FreeGb })
}
$held = $false
$firstFile = Join-Path $MachineTemp 'tgb\BUILD-FIRST'
$firstTold = $false
$firstAt = [datetime]::MinValue
function Set-First { if ($First -and ((Get-Date) - $script:firstAt).TotalSeconds -ge 10) { try { New-Item -ItemType Directory -Force -Path (Split-Path $firstFile) | Out-Null; Set-Content -LiteralPath $firstFile -Value "$PID $BuildDir" -Encoding ASCII; $script:firstAt = Get-Date } catch { } } }
# Whether another run, the coordinator's, goes first right now.
function Test-Behind {
    if ($First) { return $false }
    $f = Get-Item -LiteralPath $firstFile -ErrorAction SilentlyContinue
    return [bool]($f -and $f.LastWriteTime -gt (Get-Date).AddSeconds(-60))
}
try {
while ($queue.Count -gt 0 -or $running.Count -gt 0) {
    Set-First
    while ($queue.Count -gt 0 -and $running.Count -lt $Throttle) {
        if (Test-Behind) {
            if (-not $firstTold) { Write-Host "$(Get-Date -Format HH:mm:ss) the coordinator's build of the main tree goes first (%TEMP%\tgb\BUILD-FIRST): no new compile here until it is done; $($queue.Count) waiting, $($running.Count) running here. Nothing is wrong: wait."; $firstTold = $true }
            break
        }
        $firstTold = $false
        if (-not (Test-Room)) {
            if (-not $held) { Write-Host "$(Get-Date -Format HH:mm:ss) waiting for room on the PC (other compiles, or memory): $($queue.Count) waiting, $($running.Count) running here."; $held = $true }
            break
        }
        $held = $false
        $job = $queue.Dequeue()
        $name = Split-Path $job.Output -Leaf
        $err = Join-Path $logs ($name + '.err.txt'); $out = Join-Path $logs ($name + '.out.txt')
        New-Item -ItemType Directory -Force -Path (Split-Path $job.Output -Parent) | Out-Null
        $process = Start-Process -FilePath $job.Exe -ArgumentList $job.Arguments -WorkingDirectory $BuildDir -WindowStyle Hidden -PassThru `
            -RedirectStandardError $err -RedirectStandardOutput $out
        $null = $process.Handle   # Keeps the exit code readable after the process has ended.
        # A compile never outranks what the owner is doing on this PC (2026-10-12 02:10: three workers' compilers and
        # Starfield stood at 90 % of the CPU together). Below normal costs nothing while the CPU is free.
        try { $process.PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal } catch { }
        $null = $running.Add([pscustomobject]@{ Job = $job; Name = $name; Process = $process; Err = $err; Started = $watch.Elapsed })
    }
    Start-Sleep -Milliseconds 500
    foreach ($r in $running.ToArray()) {
        if (-not $r.Process.HasExited) { continue }
        $running.Remove($r)
        $seconds = [int]($watch.Elapsed - $r.Started).TotalSeconds
        if ($r.Process.ExitCode -ne 0) {
            $failed.Add($r.Name)
            Remove-Item -LiteralPath $r.Job.Output -Force -ErrorAction SilentlyContinue
            Write-Host "$(Get-Date -Format HH:mm:ss) FAILED $($r.Name) after $seconds s:"
            Get-Content -LiteralPath $r.Err -ErrorAction SilentlyContinue | Select-Object -First 12 | ForEach-Object { Write-Host "    $_" }
        } else {
            Write-Host "$(Get-Date -Format HH:mm:ss) $($r.Name) in $seconds s ($($queue.Count) waiting, $($running.Count) running)."
        }
    }
}
}
finally { if ($First) { Remove-Item -LiteralPath $firstFile -Force -ErrorAction SilentlyContinue } }
Write-Host "$(Get-Date -Format HH:mm:ss) shader headers done in $([int]$watch.Elapsed.TotalSeconds) s: $($stale.Count - $failed.Count) compiled, $($failed.Count) failed."
if ($failed.Count -gt 0) { exit 1 }
