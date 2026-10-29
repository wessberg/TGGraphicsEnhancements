$ErrorActionPreference='Stop'
function Assert-Free {
    if (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) { throw 'A game is running.' }
    if (Test-Path artifacts/harness/RUNNING) { throw 'The shared harness is busy.' }
}
function Build-Nr([string]$log) {
    $ErrorActionPreference='Continue'
    & .\.testruns\nr\build.cmd > $log 2>&1
    if ($LASTEXITCODE) { throw 'NR build failed.' }
}
Assert-Free
$source=Join-Path (Get-Location) 'native/neural/src/extension.cpp'
$original=[IO.File]::ReadAllBytes($source)
$body=[Text.Encoding]::UTF8.GetString($original)
$needle='rgb/=max(1-peak(base),1.0/65504.0)*inputScale;'
if (($body.Split(@($needle),[StringSplitOptions]::None)).Count -ne 2) { throw 'Output inverse not unique.' }
$mutated=$body.Replace($needle,'rgb/=max(1-peak(base),1.0/65504.0);')
try {
    [IO.File]::WriteAllText($source,$mutated,[Text.UTF8Encoding]::new($false))
    Build-Nr '.testruns/nr/signal-mutant-build.log'
    Assert-Free
    try { & .\.testruns\nr\run-stage-probe.ps1 -Name signal-mutant; throw 'Mutant incorrectly passed.' }
    catch {
        $log=Get-Content -LiteralPath '.testruns/nr/signal-mutant.log' -Raw
        if (-not $log.Contains('FAIL: Input conditioning inverse changed near-neutral HDR')) { throw }
        Write-Output 'PASS: omitting inverse input scaling fails the real HDR output assertion.'
    }
} finally {
    if ([IO.File]::ReadAllText($source) -ne $mutated) { throw 'NR source changed concurrently; refusing whole-file restoration.' }
    [IO.File]::WriteAllBytes($source,$original)
    Build-Nr '.testruns/nr/signal-restored-build.log'
}
