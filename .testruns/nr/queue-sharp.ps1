$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $repo
Write-Output 'Waiting for FoA/Starfield to close before NR deployment and the tavern comparison.'
$deadline = [DateTime]::UtcNow.AddHours(4)
while (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) {
    if (Test-Path -LiteralPath 'artifacts/harness/STOP') { throw 'Stopped before deployment.' }
    if ([DateTime]::UtcNow -gt $deadline) { throw 'Timed out waiting for the games to close; nothing deployed.' }
    Start-Sleep -Seconds 5
}
if (Test-Path -LiteralPath 'artifacts/harness/RUNNING') { throw 'Another coordinator owns the harness; nothing deployed.' }
& '.\.testruns\nr\deploy-tuning.ps1'
& '.\tools\harness\Run-PtHarness.ps1' -Plan build/nr/tuning/motion-diagnosis.json -Scenario tavern-room -Out artifacts/harness/nr-sharp-placement-20261007 -AllowGpuContention -KeepFrames -TimeoutMinutes 20
exit $LASTEXITCODE
