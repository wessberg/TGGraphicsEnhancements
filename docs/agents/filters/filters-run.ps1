# PT-75/PT-80 (filters): runs tgpt_gpu_test from the filters build, one run after another, each once the GPU is free
# (the game closed, no harness run, the MAIN tree's Run-PtHarness.ps1 -GpuCheck exits 0).
#   -Runs "tag|ENV=VALUE;ENV=VALUE" ...   each run: its tag (the output file's name) and its environment
# Output: %TEMP%\tgb\filters-out\<tag>.txt, ending with "EXIT <code>". Ends by itself; leaves no waiting shell.
#   -Rt, -Stage: the staged runtime folder under %TEMP%\tgb and the stage folder beside the exe (another pair for a second queue at once)
param([string]$Runs, [string]$RunsFile, [string]$Rt = 'filters-rt', [string]$Stage = 'filters-stage', [string]$Snap, [int]$MaxWaitMinutes = 240)   # -Runs: one string, the runs separated by commas; -RunsFile: a file holding it
if ($RunsFile) { $Runs = (Get-Content -LiteralPath $RunsFile -Raw).Trim() }
$M = 'C:\Users\Frede\Developer\TGFOA\TGFrameGeneration'
$build = Join-Path $env:TEMP 'tgb\filters-build'
$rt = Join-Path (Join-Path $env:TEMP 'tgb') $Rt
$outDir = Join-Path $env:TEMP 'tgb\filters-out'
# -Snap <name>: the queue runs from a copy of the staged runtime and its exe taken now (%TEMP%\tgb\filters-snap-<name>), so
# a build made while the queue runs changes nothing in it, and the build's own exe stays free for the linker.
if ($Snap) {
    $snapDir = Join-Path $env:TEMP ('tgb\filters-snap-' + $Snap)
    if (Test-Path -LiteralPath $snapDir) { Remove-Item -LiteralPath $snapDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $snapDir | Out-Null
    Copy-Item -Path (Join-Path $rt '*') -Destination $snapDir -Recurse -Force
    $rt = $snapDir; $build = $snapDir
}
$exe = Join-Path $build 'tgpt_gpu_test.exe'
$lock = Join-Path $M 'artifacts\harness\RUNNING'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
# A snapshot's queue keeps its own pass cache (two queues of two builds may run side by side).
$env:TGPT_SHADER_CACHE = Join-Path $env:TEMP ('tgb\filters-shadercache' + $(if ($Snap) { '-' + $Snap } else { '' }))
$env:TGPT_TEST_STARTUP_SECONDS = '900'
$env:TGPT_TEST_STAGE = $Stage
function Wait-Gpu {
    $deadline = (Get-Date).AddMinutes($MaxWaitMinutes)
    while ((Get-Date) -lt $deadline) {
        $busy = $false
        if (Get-Process -Name 'Fall of Avalon*', 'FoA*', 'TaintedGrail*', 'Tainted Grail*' -ErrorAction SilentlyContinue) { $busy = $true }
        if (-not $busy -and (Test-Path -LiteralPath $lock)) {
            $holder = 0
            [int]::TryParse(((Get-Content -LiteralPath $lock -ErrorAction SilentlyContinue | Select-Object -First 1) -split ' ')[0], [ref]$holder) | Out-Null
            if ($holder -ne 0 -and (Get-Process -Id $holder -ErrorAction SilentlyContinue)) { $busy = $true }
        }
        if (-not $busy) {
            & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $M 'tools\harness\Run-PtHarness.ps1') -GpuCheck *> $null
            if ($LASTEXITCODE -eq 0) { return $true }
        }
        Start-Sleep -Seconds 15
    }
    return $false
}
foreach ($run in ($Runs -split ',')) {
    $tag, $envText = $run -split '\|', 2
    $out = Join-Path $outDir ($tag + '.txt')
    $set = @()
    if ($envText) { foreach ($pair in ($envText -split ';')) { if ($pair -match '^([^=]+)=(.*)$') { Set-Item -Path ("Env:" + $Matches[1]) -Value $Matches[2]; $set += $Matches[1] } } }
    if (-not (Wait-Gpu)) { "NO GPU SLOT within $MaxWaitMinutes minutes" | Set-Content -LiteralPath $out; "EXIT -1" | Add-Content -LiteralPath $out; continue }
    $images = Join-Path $outDir ($tag + '-images')
    New-Item -ItemType Directory -Force -Path $images | Out-Null
    "RUN $tag  env: $envText  started $(Get-Date -Format HH:mm:ss)" | Out-File -LiteralPath $out -Encoding utf8
    & $exe $rt $images 2>&1 | Out-File -LiteralPath $out -Encoding utf8 -Append
    "EXIT $LASTEXITCODE  ended $(Get-Date -Format HH:mm:ss)" | Out-File -LiteralPath $out -Encoding utf8 -Append
    foreach ($name in $set) { Remove-Item -Path ("Env:" + $name) -ErrorAction SilentlyContinue }
}
