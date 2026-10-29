$ErrorActionPreference='Stop'
if (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) { throw 'A game is running.' }
if (Test-Path artifacts/harness/RUNNING) { throw 'The shared game harness is busy.' }
& .\tools\harness\Run-PtHarness.ps1 -GpuCheck
if ($LASTEXITCODE) { throw 'The GPU is busy.' }
$variants=@(@('-1','unset'),@('0','zero'),@('0.5','half'),@('1','one'),@('-1','unset-repeat'))
foreach ($variant in $variants) {
    if (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) { throw 'A game started.' }
    if (Test-Path artifacts/harness/RUNNING) { throw 'The shared game harness became busy.' }
    & .\build\nr\global-tone-check\probe.exe build/nr/global-tone-check/BepInEx $variant[0] $variant[1]
    if ($LASTEXITCODE) { throw "Global Tone probe failed: $($variant[1])" }
}
