param([string]$Runs='base,after')
# glints worker: several runs of the glints fixture one after another, in ONE waiting script (glints-gpu.ps1 waits for the
# GPU before each). -Runs: names separated by commas. A name is <tracer>[-f<frames>][-o<options>][-s<set>]: the tracer
# "base" is the build without the change (glints-base), anything else this worker's build as it is when the run starts.
# Example: -Runs "base-f1024-o2048,after-f1024-o2048"
$ErrorActionPreference='Continue'
$tgb='C:\Users\Frede\AppData\Local\Temp\tgb'
foreach($run in ($Runs -split ',')) {
    $run=$run.Trim(); if(!$run) { continue }
    # "partb...": the build with the lobe's own draw alone (part B), without the pixel's own candidates (part A).
    $dll=if($run -like 'base*') { "$tgb\glints-base\TGPathTracing.Native.dll" } elseif($run -like 'partb*') { "$tgb\glints-partB\TGPathTracing.Native.dll" } else { "$tgb\glints\TGPathTracing.Native.dll" }
    Write-Output ("==== run "+$run+" ====")
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',"$tgb\glints-gpu.ps1",'-Name',$run,'-NativeDll',$dll)
    if($run -match '-f(\d+)') { $arguments+=@('-Frames',$Matches[1]) }
    if($run -match '-o(\d+)') { $arguments+=@('-Options',$Matches[1]) }
    if($run -match '-s([a-z]+)') { $arguments+=@('-Set',$Matches[1]) }
    & powershell @arguments
    Write-Output ("==== run "+$run+" exit "+$LASTEXITCODE+" ====")
}
