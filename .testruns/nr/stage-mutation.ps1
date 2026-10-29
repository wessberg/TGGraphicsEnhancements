$ErrorActionPreference='Stop'
function Build-Nr([string]$log) {
    $ErrorActionPreference='Continue' # PowerShell 5 treats CMake's stderr warnings as errors with redirection.
    & .\.testruns\nr\build.cmd > $log 2>&1
    if ($LASTEXITCODE) { throw 'NR build failed; do not deploy.' }
}
if (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) { throw 'A game is running.' }
if (Test-Path artifacts/harness/RUNNING) { throw 'The harness is busy.' }
$source=Join-Path (Get-Location) 'native/neural/src/diagnostics.hpp'
$original=[IO.File]::ReadAllBytes($source)
$body=[Text.Encoding]::UTF8.GetString($original)
if (-not $body.Contains('if(flip) p.y=')) { throw 'Expected orientation expression is absent.' }
try {
    [IO.File]::WriteAllText($source,$body.Replace('if(flip) p.y=','if(!flip) p.y='),[Text.UTF8Encoding]::new($false))
    Build-Nr '.testruns/nr/stage-mutant-build.log'
    Copy-Item -LiteralPath build/nr/install/TGNeuralRendering/TGNeuralRendering.Native.dll -Destination build/nr/probe/BepInEx/TGNeuralRendering/Native/TGNeuralRendering.Native.dll
    & .\build\nr\tgnr_gpu_smoke.exe build/nr/probe/BepInEx --diagnostics build/nr/stage-probe-wrong-y > .testruns/nr/stage-mutant-gpu.log 2>&1
    if ($LASTEXITCODE) { throw 'Mutant failed outside the diagnostic check.' }
    $checkExit = & {
        $ErrorActionPreference='Continue'
        & python tests/neural_stage_probe.py build/nr/stage-probe-wrong-y > .testruns/nr/stage-mutant-check.log 2>&1
        $LASTEXITCODE
    }
    if ($checkExit -eq 0) { throw 'The diagnostic accepted a wrong Y orientation.' }
    Write-Output 'PASS: the wrong-Y GPU diagnostic was rejected.'
} finally {
    [IO.File]::WriteAllBytes($source,$original)
    Build-Nr '.testruns/nr/stage-restored-build.log'
    Copy-Item -LiteralPath build/nr/install/TGNeuralRendering/TGNeuralRendering.Native.dll -Destination build/nr/probe/BepInEx/TGNeuralRendering/Native/TGNeuralRendering.Native.dll
}
