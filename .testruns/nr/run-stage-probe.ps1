param([Parameter(Mandatory=$true)][ValidatePattern('^[a-zA-Z0-9-]+$')][string]$Name)
$ErrorActionPreference='Stop'
function Assert-ProbeAvailable {
    if (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) { throw 'A game is running.' }
    if (Test-Path artifacts/harness/RUNNING) { throw 'The shared harness is busy.' }
}
Assert-ProbeAvailable
$output=Join-Path 'build/nr' $Name
if (Test-Path -LiteralPath $output) { throw 'Use a new probe output directory.' }
Copy-Item -LiteralPath build/nr/install/TGNeuralRendering/TGNeuralRendering.Native.dll -Destination build/nr/probe/BepInEx/TGNeuralRendering/Native/TGNeuralRendering.Native.dll
Assert-ProbeAvailable # Check at execution time, including after an approval wait.
$ErrorActionPreference='Continue'
& .\build\nr\tgnr_gpu_smoke.exe build/nr/probe/BepInEx --diagnostics $output > ".testruns/nr/$Name.log" 2>&1
if ($LASTEXITCODE) { throw 'NR probe failed.' }
& python tests/neural_stage_probe.py $output
if ($LASTEXITCODE) { throw 'NR readback validation failed.' }
