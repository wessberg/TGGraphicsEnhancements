param([string]$Out='artifacts/harness/nr-validation-standalone-r15')
$ErrorActionPreference='Stop'
$nrRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Set-Location -LiteralPath $nrRepo
$nrGame='C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA'
$nrLock=Join-Path $nrRepo 'artifacts/harness/RUNNING'
if (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) { throw 'A game is running. Nothing changed.' }
$nrLease=[IO.File]::Open($nrLock,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try { $nrBytes=[Text.Encoding]::ASCII.GetBytes("$PID nr-standalone $([DateTime]::Now.ToString('s'))"); $nrLease.Write($nrBytes,0,$nrBytes.Length) }
finally { $nrLease.Dispose() }
$nrBackup=[IO.Path]::GetFullPath((Join-Path $nrGame ('BepInEx/TGNeuralRendering/ValidationBackup/'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))))
$nrJournal=Join-Path $nrRepo 'build/nr/validation-r15/standalone-files.json'
$nrFiles=@()
try {
    if (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) { throw 'A game started before staging. Nothing changed.' }
    foreach ($nrMod in @('TGFrameGeneration','TGBetterUpscaling','TGPathTracing')) {
        foreach ($nrRelative in @("BepInEx/plugins/$nrMod/$nrMod.dll", "BepInEx/plugins/$nrMod/TGGraphicsBridge.dll", "BepInEx/patchers/$nrMod.Autoload.dll")) {
            $nrSource=[IO.Path]::GetFullPath((Join-Path $nrGame $nrRelative))
            $nrTarget=[IO.Path]::GetFullPath((Join-Path $nrBackup $nrRelative))
            # No recursive move, no shell-built paths, and no overwrites of Vortex's staging targets.
            if (-not $nrSource.StartsWith($nrGame+'\',[StringComparison]::OrdinalIgnoreCase) -or -not $nrTarget.StartsWith($nrBackup+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Staging path escaped its game/backup folder.' }
            if (-not (Test-Path -LiteralPath $nrSource)) { throw "Expected installed file missing: $nrSource" }
            $nrFiles+=@{source=$nrSource; backup=$nrTarget; hash=(Get-FileHash -LiteralPath $nrSource).Hash; restored=$false}
        }
    }
    $nrFiles | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $nrJournal -Encoding UTF8
    foreach ($nrFile in $nrFiles) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $nrFile.backup) -Force | Out-Null
        Move-Item -LiteralPath $nrFile.source -Destination $nrFile.backup
    }
    & .\tools\harness\Run-PtHarness.ps1 -Plan build/nr/validation-r15/standalone.json -Scenario tavern-room -Out $Out -KeepFrames -KeepHud -KeepLock -TimeoutMinutes 10 -GpuWaitMinutes 10
    if ($LASTEXITCODE -ne 0) { throw 'Standalone run failed. Files will still be restored.' }
} finally {
    $nrRestoreErrors=@()
    foreach ($nrFile in $nrFiles) {
        try {
            if (Test-Path -LiteralPath $nrFile.backup) {
                if (Test-Path -LiteralPath $nrFile.source) { throw "Another file appeared while staged. Preserving both copies: $($nrFile.source)" }
                Move-Item -LiteralPath $nrFile.backup -Destination $nrFile.source
            }
            if ((Get-FileHash -LiteralPath $nrFile.source).Hash -ne $nrFile.hash) { throw "Restored file hash differs: $($nrFile.source)" }
            $nrFile.restored=$true
        }
        catch { $nrRestoreErrors+=$_.Exception.Message }
    }
    $nrFiles | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $nrJournal -Encoding UTF8
    if ((Test-Path -LiteralPath $nrLock) -and ((Get-Content -LiteralPath $nrLock | Select-Object -First 1) -like "$PID *")) { Remove-Item -LiteralPath $nrLock }
    if ($nrRestoreErrors.Count) { throw ($nrRestoreErrors -join "`n") }
    Write-Output 'All standalone staging files restored and hash-checked.'
}
