param([ValidateSet('Full','Tone','All')][string]$Cases='All')
$ErrorActionPreference='Stop'
if (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) { throw 'A game is running.' }
if (Test-Path artifacts/harness/RUNNING) { throw 'The shared game harness is busy.' }
& .\tools\harness\Run-PtHarness.ps1 -GpuCheck
if ($LASTEXITCODE) { throw 'The GPU is busy.' }
$full=@(@('0','guided'),@('1','no-motion'),@('2','no-depth'),@('3','reset-each'),@('0','guided-repeat'))
$tone=@(@('4','tone-guided'),@('5','tone-no-motion'),@('6','tone-reset-each'))
$variants=if ($Cases -eq 'Full') { $full } elseif ($Cases -eq 'Tone') { $tone } else { $full+$tone }
foreach ($variant in $variants) {
    if (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) { throw 'A game started.' }
    if (Test-Path artifacts/harness/RUNNING) { throw 'The shared game harness became busy.' }
    & .\build\nr\guide-use-check\probe.exe build/nr/guide-use-check/BepInEx $variant[0] $variant[1]
    if ($LASTEXITCODE) { throw "Guide consumption probe failed: $($variant[1])" }
}
