param([switch]$MenuOnly, [switch]$DeployNr, [string]$Out='artifacts/harness/nr-validation-menu-r15')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $repo
$deadline=[DateTime]::UtcNow.AddMinutes(30)
$lastState=''
while ($true) {
    if ((Test-Path artifacts/harness/STOP) -or (Test-Path build/nr/validation-r15/STOP)) { throw 'Validation queue cancelled.' }
    if ([DateTime]::UtcNow -gt $deadline) { throw 'Timed out waiting for the games and shared harness. No game was launched.' }
    $games=@(Get-Process -Name 'Starfield','Fall of Avalon' -ErrorAction SilentlyContinue)
    $held=Test-Path artifacts/harness/RUNNING
    $state="Waiting: shared harness=$held, games=$($games.ProcessName -join ', ')"
    if (-not $held -and $games.Count -eq 0) { break }
    if ($state -ne $lastState) { Write-Output $state; $lastState=$state }
    Start-Sleep -Seconds 5
}
if (-not $MenuOnly) {
    & .\.testruns\nr\validation-gpu.cmd
    if ($LASTEXITCODE -ne 0) { throw 'GPU validation stopped. No game was launched.' }
    & python .testruns/nr/validation-fallbacks.py
    if ($LASTEXITCODE -ne 0) { throw 'Fallback/coexistence validation stopped. No game was launched.' }
}
if ($DeployNr) { & .\.testruns\nr\deploy-tuning.ps1 }
# Run-PtHarness owns the lock, deployment of its own plugin, GPU gate and config restoration.
& .\tools\harness\Run-PtHarness.ps1 -Plan build/nr/validation-r15/menu.json -Scenario tavern-room -Out $Out -KeepFrames -KeepHud -TimeoutMinutes 10 -GpuWaitMinutes 10
if ($LASTEXITCODE -ne 0) { throw 'Live menu validation failed. Inspect its report before continuing.' }
Write-Output 'GPU and menu validation finished. Image review and remaining live combinations still required.'
