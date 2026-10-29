$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $repo
$deadline = [DateTime]::UtcNow.AddHours(2)
$lastState = ''
Write-Output 'NR chair comparison queued behind the shared harness and both games. No game files change while waiting.'
while ($true) {
    if (Test-Path -LiteralPath 'artifacts/harness/STOP') { throw 'Stopped before the chair comparison.' }
    if (Test-Path -LiteralPath 'build/nr/chairs-stop') { throw 'NR chair queue cancelled.' }
    if ([DateTime]::UtcNow -gt $deadline) { throw 'NR queue timed out. No game was started.' }
    $games = @(Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue)
    $held = $false
    $lockText = ''
    if (Test-Path -LiteralPath 'artifacts/harness/RUNNING') {
        $lockText = [string](Get-Content -LiteralPath 'artifacts/harness/RUNNING' -ErrorAction SilentlyContinue | Select-Object -First 1)
        $holder = 0
        [void][int]::TryParse(($lockText -split ' ')[0], [ref]$holder)
        $held = $holder -ne 0 -and $null -ne (Get-Process -Id $holder -ErrorAction SilentlyContinue)
    }
    if (-not $held -and $games.Count -eq 0) { break }
    $state = "Waiting: harness=$held ($lockText), games=$($games.ProcessName -join ', ')"
    if ($state -ne $lastState) { Write-Output $state; $lastState = $state }
    Start-Sleep -Seconds 5
}
& python tools/harness/neural_chairs.py --output build/nr/chairs-plan.json
if ($LASTEXITCODE -ne 0) { throw 'Chair plan preparation failed.' }
$out = 'artifacts/harness/nr-chairs-quality-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
[IO.File]::WriteAllText((Join-Path $repo 'build/nr/chairs-queued-output.txt'), $out)
# This launcher alone owns deployment, the atomic harness lock, GPU gating, save protection and restoration.
& '.\tools\harness\Run-PtHarness.ps1' -Plan build/nr/chairs-plan.json -Out $out -KeepFrames -TimeoutMinutes 12 -GpuWaitMinutes 10
if ($LASTEXITCODE -ne 0) { throw "Chair capture failed; inspect $out. No automatic retry." }
foreach ($script in @('neural_stages.py','neural_temporal.py')) {
    & python (Join-Path 'tools/harness' $script) $out
    if ($LASTEXITCODE -ne 0) { throw "$script rejected $out." }
}
& python tools/harness/neural_chairs.py --report $out
if ($LASTEXITCODE -ne 0) { throw "Chair route or neutral-stage validation rejected $out." }
Write-Output "Chair comparison captured and analysed: $out. Image review and a stability verdict still require reading the atlas."
