# SPDX-License-Identifier: MIT
# The worker psr-2's small GPU check, alone: tgpt_gpu_test with TGPT_TEST_PSR_ONLY=1 (tests/pathtracing_psr_checks.inl)
# against one or more builds of the tracer's DLL, one after the other. Never the whole tgpt_gpu_test.
#
#   .\tools\psr\run-check.ps1 -Runs 'C', 'A:4194304', 'A:0', 'B:0'
#
# A run is <variant>[:<frame flag bits>]: the DLL %TEMP%\tgb\psr-build\variants\<variant>\TGPathTracing.Native.dll; with
# flag bits the test takes it for a tracer from before the rule (TGPT_TEST_PSR_OLD: numbers only).
# Before each run it waits until the game is closed and the MAIN tree's tools\harness\Run-PtHarness.ps1 -GpuCheck says the
# GPU is free (the coordinator's game, another program of the owner's), and it writes that check's answer before and after
# into the run's log: a time taken beside another program on the card means nothing.
# WHILE a run goes it looks every three seconds whether the game has started (a cold pass cache makes the test's start
# take minutes, and the coordinator's run may begin in them): then it stops the test at once, waits for the card to be
# free again and starts the run over (the passes made so far are in the cache). Our test never runs beside the game.
# It also notes in the log when another worker's offline GPU test ran beside it (up to four may): the checks stand, the
# times do not.
# Everything it writes lies under %TEMP%\tgb\psr-build: the staged folders, the tracer's pass cache (TGPT_SHADER_CACHE:
# not the game's), the pictures and the logs.
param(
    [Parameter(Mandatory = $true)][string[]]$Runs,
    [string]$Build = 'C:\Users\Frede\AppData\Local\Temp\tgb\psr-build',
    [string]$Main = 'C:\Users\Frede\Developer\TGFOA\TGFrameGeneration',
    [int]$WaitMinutes = 120,
    [int]$Tries = 8,
    [string]$Tag = ''
)
$ErrorActionPreference = 'Stop'
$harness = Join-Path $Main 'tools\harness\Run-PtHarness.ps1'
function Test-Game { return [bool](Get-Process -Name 'Fall of Avalon' -ErrorAction SilentlyContinue) }
function Test-GpuFree {
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File $harness -GpuCheck 2>&1 | Out-String
    return [pscustomobject]@{ Free = ($LASTEXITCODE -eq 0) -and -not (Test-Game); Text = $text }
}
foreach ($run in $Runs) {
    $parts = $run -split ':'
    $variant = $parts[0]
    $old = if ($parts.Count -gt 1) { $parts[1] } else { $null }
    $name = if ($null -ne $old) { "$variant-old$old" } else { $variant }
    if ($Tag) { $name = "$name-$Tag" }
    $log = Join-Path $Build "check-$name.log"
    $dll = Join-Path $Build "variants\$variant\TGPathTracing.Native.dll"
    if (-not (Test-Path -LiteralPath $dll)) { "no $dll" | Set-Content -LiteralPath $log -Encoding utf8; continue }
    $root = Join-Path $Build "roots\$variant"
    foreach ($mod in 'TGGraphicsBridge', 'TGPathTracing') {
        $to = Join-Path $root $mod
        if (-not (Test-Path -LiteralPath $to)) {
            New-Item -ItemType Directory -Force -Path $to | Out-Null
            Copy-Item -Path (Join-Path $Main "artifacts\Native\$mod\*") -Destination $to -Recurse -Force -Exclude '*.log'
        }
    }
    Copy-Item -LiteralPath $dll -Destination (Join-Path $root 'TGPathTracing\TGPathTracing.Native.dll') -Force
    $hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
    "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') run $name, the tracer's DLL $hash" | Set-Content -LiteralPath $log -Encoding utf8
    $deadline = (Get-Date).AddMinutes($WaitMinutes)
    $done = $false
    for ($try = 1; $try -le $Tries -and -not $done; ++$try) {
        $gpu = Test-GpuFree
        while (-not $gpu.Free -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 20; $gpu = Test-GpuFree }
        "--- try $try, the GPU before the run ($(Get-Date -Format HH:mm:ss)):`r`n$($gpu.Text)" | Add-Content -LiteralPath $log -Encoding utf8
        if (-not $gpu.Free) { "The GPU did not come free within $WaitMinutes minutes: not run." | Add-Content -LiteralPath $log -Encoding utf8; break }
        $env:TGPT_TEST_PSR_ONLY = '1'
        $env:TGPT_TEST_STAGE = "tgpt-native-$variant"
        $env:TGPT_TEST_STARTUP_SECONDS = '900'
        $env:TGPT_SHADER_CACHE = Join-Path $Build 'pass-cache'
        if ($null -ne $old) { $env:TGPT_TEST_PSR_OLD = $old } else { Remove-Item Env:TGPT_TEST_PSR_OLD -ErrorAction SilentlyContinue }
        $images = Join-Path $Build "images-$name"
        $out = Join-Path $Build "check-$name.out.txt"
        $err = Join-Path $Build "check-$name.err.txt"
        $process = Start-Process -FilePath (Join-Path $Build 'tgpt_gpu_test.exe') -ArgumentList "`"$root`"", "`"$images`"" -WorkingDirectory $Build `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
        $null = $process.Handle
        $stopped = $false
        $others = $false
        while (-not $process.WaitForExit(3000)) {
            if (Test-Game) { try { $process.Kill() } catch { }; $process.WaitForExit(); $stopped = $true; break }
            if (Get-Process -Name 'tgpt_gpu_test' -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $process.Id }) { $others = $true }
        }
        if ($stopped) {
            "--- try ${try}: the game started while the test ran ($(Get-Date -Format HH:mm:ss)): the test was stopped, the run starts over when the card is free." |
                Add-Content -LiteralPath $log -Encoding utf8
            Remove-Item -LiteralPath $out, $err -ErrorAction SilentlyContinue
            continue
        }
        Get-Content -LiteralPath $out | Add-Content -LiteralPath $log -Encoding utf8
        if ((Get-Item -LiteralPath $err).Length -gt 0) { Get-Content -LiteralPath $err | Add-Content -LiteralPath $log -Encoding utf8 }
        "--- exit code $($process.ExitCode) ($(Get-Date -Format HH:mm:ss))" | Add-Content -LiteralPath $log -Encoding utf8
        if ($others) { "--- ANOTHER WORKER'S GPU TEST RAN BESIDE THIS ONE: its checks stand, its times (the mirrors' cost lines) mean nothing." | Add-Content -LiteralPath $log -Encoding utf8 }
        $after = Test-GpuFree
        "--- the GPU after the run:`r`n$($after.Text)" | Add-Content -LiteralPath $log -Encoding utf8
        Remove-Item -LiteralPath $out, $err -ErrorAction SilentlyContinue
        $done = $true
    }
}
