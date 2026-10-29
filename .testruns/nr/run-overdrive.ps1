$ErrorActionPreference='Stop'
$nrCheckRoot=Join-Path (Get-Location) 'build/nr/overdrive-check'
try {
    foreach ($nrCase in @(@('baseline','1'),@('repeat','1'),@('onehalf','1.5'),@('double','2'))) {
        if (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) { throw 'A game is running.' }
        if (Test-Path artifacts/harness/RUNNING) { throw 'The shared harness is busy.' }
        $env:TGNR_PROBE_CASE=$nrCase[0]
        $env:TGNR_PROBE_INTENSITY=$nrCase[1]
        if (Test-Path (Join-Path $nrCheckRoot $nrCase[0])) { throw 'Probe case already exists.' }
        & (Join-Path $nrCheckRoot 'probe.exe') (Join-Path $nrCheckRoot 'BepInEx') > (Join-Path $nrCheckRoot ($nrCase[0]+'.log')) 2>&1
        if ($LASTEXITCODE) { throw ('Probe failed: '+$nrCase[0]) }
        Write-Output ('Completed native intensity factor '+$nrCase[1]+' ('+$nrCase[0]+')')
    }
} finally {
    Remove-Item Env:TGNR_PROBE_CASE -ErrorAction SilentlyContinue
    Remove-Item Env:TGNR_PROBE_INTENSITY -ErrorAction SilentlyContinue
}
