param([string]$Name='after', [string]$NativeDll='C:\Users\Frede\AppData\Local\Temp\tgb\glints\TGPathTracing.Native.dll', [string]$Frames='', [string]$Set='', [string]$Options='', [int]$MaxWaitMinutes=240)
# glints worker: run the tracer's offline GPU test with TGPT_TEST_GLINTS_ONLY=1 (the glints fixture alone) on this worker's
# own build, once the game is closed, no harness run is on and the main tree's -GpuCheck says the GPU is free. One waiting
# loop, inside this one script. Never starts the game, never writes outside %TEMP%\tgb\glints*.
# -NativeDll: the tracer's DLL under test (glints-base\... is the build without the change).
$ErrorActionPreference='Stop'
$main='C:\Users\Frede\Developer\TGFOA\TGFrameGeneration'
$build='C:\Users\Frede\AppData\Local\Temp\tgb\glints'
$runtime='C:\Users\Frede\AppData\Local\Temp\tgb\glints-runtime-'+$Name
$out='C:\Users\Frede\AppData\Local\Temp\tgb\glints-out-'+$Name
New-Item -ItemType Directory -Force -Path $runtime,$out | Out-Null
foreach($mod in @('TGGraphicsBridge','TGPathTracing')) {
    if(!(Test-Path "$runtime\$mod")) { Copy-Item -LiteralPath "$main\artifacts\Native\$mod" -Destination $runtime -Recurse }
}
Copy-Item -LiteralPath $NativeDll -Destination "$runtime\TGPathTracing\TGPathTracing.Native.dll" -Force
Copy-Item -LiteralPath "$build\tgpt_gpu_test.exe" -Destination "$runtime\tgpt_gpu_test.exe" -Force
Write-Output ('Native under test '+(Get-FileHash "$runtime\TGPathTracing\TGPathTracing.Native.dll").Hash.Substring(0,16))
function BusyReason {
    $game=Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match 'Fall.?of.?Avalon|Tainted.?Grail' }
    $reason=''
    if($game) { $reason='Waiting: the game is running.' }
    $marker="$main\artifacts\harness\RUNNING"
    if(!$reason -and (Test-Path $marker)) {
        $text=Get-Content -LiteralPath $marker -Raw
        $ownerPid=0
        if($text -match '^\s*(\d+)') { $ownerPid=[int]$Matches[1] }
        if(!$ownerPid -or (Get-Process -Id $ownerPid -ErrorAction SilentlyContinue)) { $reason='Waiting: a harness run is on (or its marker cannot be read).' }
    }
    return $reason
}
Get-ChildItem Env: | Where-Object { $_.Name -like 'TGPT_TEST_*' } | ForEach-Object { Remove-Item ("Env:"+$_.Name) -ErrorAction SilentlyContinue }
$env:TGPT_TEST_GLINTS_ONLY='1'
if($Frames) { $env:TGPT_TEST_GLINTS_FRAMES=$Frames }
if($Set) { $env:TGPT_TEST_GLINTS_SET=$Set }
if($Options) { $env:TGPT_TEST_GLINTS_OPTIONS=$Options }
$env:TGPT_TEST_STARTUP_SECONDS='900' # a new build's first start compiles its passes for minutes
$env:TGPT_TEST_STAGE='glints-'+$Name
$deadline=(Get-Date).AddMinutes($MaxWaitMinutes)
$last=''
while($true) {
    if((Get-Date) -gt $deadline) { Write-Output 'Gave up waiting for the GPU (the time limit of this script).'; exit 98 }
    $reason=BusyReason
    if(!$reason) {
        & powershell -NoProfile -ExecutionPolicy Bypass -File "$main\tools\harness\Run-PtHarness.ps1" -GpuCheck *> "$out\gpu-check.log"
        if($LASTEXITCODE -ne 0) { $reason='Waiting: the GPU check has not cleared.' }
    }
    if($reason) {
        if($reason -ne $last) { Write-Output ((Get-Date -Format 'HH:mm:ss')+' '+$reason); $last=$reason }
        Start-Sleep -Seconds 10
        continue
    }
    Write-Output ((Get-Date -Format 'HH:mm:ss')+' the test starts')
    $process=Start-Process -FilePath "$runtime\tgpt_gpu_test.exe" -ArgumentList ('"'+$runtime+'" "'+$out+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out\gpu.log" -RedirectStandardError "$out\gpu-errors.log"
    $ownedHandle=$process.Handle
    $retry=$false
    while(!$process.WaitForExit(500)) {
        $reason=BusyReason
        if($reason) {
            if(!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
            Write-Output ((Get-Date -Format 'HH:mm:ss')+' '+$reason+' Stopped only this worker test; it will retry.')
            $retry=$true
            break
        }
    }
    if($retry) { $process.Dispose(); Start-Sleep -Seconds 10; continue }
    $process.WaitForExit()
    $code=$process.ExitCode
    $process.Dispose()
    Write-Output ((Get-Date -Format 'HH:mm:ss')+' the test ended')
    Select-String -LiteralPath "$out\gpu.log" -Pattern '^FAIL|^FAILED|stopped at|checks passed|glints|passes made in|last error|failed frames|frames traced' |
        ForEach-Object { $_.Line } | Group-Object -NoElement | ForEach-Object { if($_.Count -gt 1) { "{0}  [x{1}]" -f $_.Name, $_.Count } else { $_.Name } }
    $native=Get-ChildItem "$runtime\glints-$Name\TGPathTracing\*.log" -ErrorAction SilentlyContinue | Select-Object -First 1
    if($native) {
        $lines=Get-Content -LiteralPath $native.FullName
        $from=0; for($i=$lines.Count-1;$i -ge 0;--$i) { if($lines[$i] -match 'extension bootstrap') { $from=$i; break } }
        if(($lines[$from..($lines.Count-1)] -match '0x887A0006|0x887A0005').Count -gt 0) { 'VOID: the GPU hung or was removed during this run (0x887A000x in the native log); run it once more.' }
    }
    if((Test-Path "$out\gpu-errors.log") -and (Get-Item "$out\gpu-errors.log").Length -gt 0) { Get-Content "$out\gpu-errors.log" -Tail 10 }
    Write-Output ("Worker test exit code: "+$code)
    exit $code
}
