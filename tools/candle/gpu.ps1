param([switch]$Full)
$ErrorActionPreference='Stop'
$main='C:/Users/Frede/Developer/TGFOA/TGFrameGeneration'
$build='C:/Users/Frede/AppData/Local/Temp/tgb/candle-build'
$stage='C:/Users/Frede/AppData/Local/Temp/tgb/candle-stage'
foreach($mod in @('TGGraphicsBridge','TGPathTracing')) { New-Item -ItemType Directory "$stage/$mod" -Force | Out-Null }
Copy-Item "$main/artifacts/Native/TGGraphicsBridge/TGGraphicsBridge.Native.dll" "$stage/TGGraphicsBridge/" -Force
Copy-Item "$build/TGPathTracing.Native.dll" "$stage/TGPathTracing/" -Force
Copy-Item "$main/artifacts/Native/TGPathTracing/D3D12" "$stage/TGPathTracing/" -Recurse -Force
Write-Output ('Native build '+(Get-FileHash "$build/TGPathTracing.Native.dll").Hash)
Write-Output ('Native stage '+(Get-FileHash "$stage/TGPathTracing/TGPathTracing.Native.dll").Hash)
$announced=$false
while($true) {
    $holder=0; $lock="$main/artifacts/harness/RUNNING"
    if(Test-Path -LiteralPath $lock) { [int]::TryParse(((Get-Content -LiteralPath $lock | Select-Object -First 1) -split ' ')[0],[ref]$holder) | Out-Null }
    $locked=$holder -gt 0 -and (Get-Process -Id $holder -ErrorAction SilentlyContinue)
    if(!$locked) {
        & powershell -NoProfile -File "$main/tools/harness/Run-PtHarness.ps1" -GpuCheck
        if($LASTEXITCODE -eq 0) { break }
    }
    if(!$announced) { Write-Output 'Waiting for the game/harness/GPU gate in this single worker script.'; $announced=$true }
    Start-Sleep -Seconds 15
}
$env:TGPT_TEST_STARTUP_SECONDS='600'
if($Full) { Remove-Item Env:TGPT_TEST_CANDLE_ONLY -ErrorAction SilentlyContinue } else { $env:TGPT_TEST_CANDLE_ONLY='1' }
& "$build/tgpt_gpu_test.exe" $stage 'C:/Users/Frede/AppData/Local/Temp/tgb/candle-images'
exit $LASTEXITCODE
