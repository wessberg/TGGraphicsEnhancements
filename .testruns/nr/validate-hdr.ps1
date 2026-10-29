param([string]$Out='artifacts/harness/nr-validation-hdr-r15')
$ErrorActionPreference='Stop'
$nrRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $nrRepo
$nrGame='C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA'
$nrLock=Join-Path $nrRepo 'artifacts/harness/RUNNING'
$nrProbe=Join-Path $nrGame 'TGNR.HdrCapture.addon64'
$nrBuilt=Join-Path $nrRepo 'build/nr/TGNR.HdrCapture.addon64'
if (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) { throw 'A game is running. Nothing changed.' }
if (Test-Path -LiteralPath $nrProbe) { throw 'HDR probe already exists. Nothing changed.' }
$nrHash=(Get-FileHash -LiteralPath $nrBuilt).Hash
$nrIni=Join-Path $nrGame 'ReShade.ini'
$nrIniBefore=[IO.File]::ReadAllBytes($nrIni)
$nrLease=[IO.File]::Open($nrLock,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try { $nrBytes=[Text.Encoding]::ASCII.GetBytes("$PID nr-hdr $([DateTime]::Now.ToString('s'))"); $nrLease.Write($nrBytes,0,$nrBytes.Length) }
finally { $nrLease.Dispose() }
try {
    if (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) { throw 'A game started before staging.' }
    Copy-Item -LiteralPath $nrBuilt -Destination $nrProbe
    if ((Get-FileHash -LiteralPath $nrProbe).Hash -ne $nrHash) { throw 'HDR probe deployment hash mismatch.' }
    & .\tools\harness\Run-PtHarness.ps1 -Plan build/nr/validation-r15/hdr.json -Scenario tavern-room -Out $Out -KeepFrames -KeepHud -KeepLock -AllowReShade -NoAnalyze -TimeoutMinutes 10 -GpuWaitMinutes 5
    if ($LASTEXITCODE -ne 0) { throw 'HDR run failed. Probe will still be removed once the game exits.' }
} finally {
    if (Get-Process -Name 'Fall of Avalon' -ErrorAction SilentlyContinue) {
        Write-Warning 'Game still running. Keeping probe and lock until it exits. No active file will be replaced.'
    } else {
        if (Test-Path -LiteralPath $nrProbe) {
            if ((Get-FileHash -LiteralPath $nrProbe).Hash -ne $nrHash) { throw 'Probe changed. Preserving it and the lock for review.' }
            Remove-Item -LiteralPath $nrProbe
        }
        # Do not write through the owner/Vortex ReShade symlink. Record any runtime INI rewrite for review.
        $nrEvidence=Join-Path $nrRepo $Out
        if (Test-Path -LiteralPath $nrEvidence) {
            [IO.File]::WriteAllBytes((Join-Path $nrEvidence 'ReShade-before.ini'),$nrIniBefore)
            Copy-Item -LiteralPath $nrIni -Destination (Join-Path $nrEvidence 'ReShade-after.ini')
            Copy-Item -LiteralPath (Join-Path $nrGame 'ReShade.log') -Destination (Join-Path $nrEvidence 'ReShade.log')
        }
        if ((Test-Path -LiteralPath $nrLock) -and ((Get-Content -LiteralPath $nrLock | Select-Object -First 1) -like "$PID *")) { Remove-Item -LiteralPath $nrLock }
        Write-Output 'Temporary HDR probe removed. ReShade and RenoDX remain enabled.'
    }
}
