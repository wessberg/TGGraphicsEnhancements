# A QUEUED run of the game: waits for a free card while it holds the harness's lock, then runs, then says what the run's
# report and the tracer's log hold. For a coordinator who has other work meanwhile (start it in the background).
#
#   .\tools\harness\Queue-PtRun.ps1 -Scenario forest@camp-walks                     the build that is in the game folder
#   .\tools\harness\Queue-PtRun.ps1 -Scenario forest@camp -TrialDll <a worker's DLL>  a trial or measuring build
#
# Why a script of its own (2026-10-12):
# - It takes artifacts/harness/RUNNING BEFORE it waits. Run-PtHarness.ps1 -GpuCheck exits 1 while a live process holds
#   that file, so workers start no new offline GPU test and the run gets the next gap (two workers' chains of tests kept
#   a run of the first priority waiting that night).
# - A trial build is put into the game folder only AFTER the card is free, and main's build (build\native) is put back
#   when the run has ended: "a measuring build is never left in the game", and a wait of unknown length (the owner's
#   game is open) is "left".
# - Without -TrialDll it refuses when the game folder's native DLL is not build\native's.
# - It reads the tracer's log from the LAST start on: the log runs on across starts, and an old "The GPU stopped" line
#   was once read as the run's own.
# -Measure brings the game to the front (frame times that may be quoted): it takes the owner's screen.
# -KeepFrames keeps the run's raw frames (33 MB each at 4K): for a moving picture made from them afterwards.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Scenario,
    [int]$TimeoutMinutes = 30,
    [switch]$Measure,
    [switch]$KeepFrames,
    [string]$TrialDll = '',
    [string]$Tag = 'queued',
    [int]$WaitHours = 3
)
$ErrorActionPreference = 'Continue'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Set-Location $repo
$env:Path = "C:\Program Files (x86)\Microsoft Visual Studio\Installer;" + $env:Path
$props = Join-Path $repo 'Build.Local.props'
$game = if (Test-Path -LiteralPath $props) { ([xml](Get-Content -LiteralPath $props -Raw)).Project.PropertyGroup.GameDirMono } else { '' }
if (-not $game -or -not (Test-Path -LiteralPath $game)) { throw 'No game folder (GameDirMono in Build.Local.props).' }
$main = Join-Path $repo 'build\native\TGPathTracing.Native.dll'
$installed = Join-Path $game 'BepInEx\TGPathTracing\Native\TGPathTracing.Native.dll'
if ($TrialDll -and -not (Test-Path -LiteralPath $TrialDll)) { throw "No such DLL: $TrialDll" }
$logDir = Join-Path $env:TEMP 'tgb'; New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$log = Join-Path $logDir "run-$Tag.log"
$lock = Join-Path $repo 'artifacts\harness\RUNNING'
$stop = Join-Path $repo 'artifacts\harness\STOP'
function Get-Short([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.Substring(0, 12) }
# The lock given back when this script ends before the harness took it over (a wait that was stopped or ran out).
function Clear-OwnLock { if ((Test-Path -LiteralPath $lock) -and ((Get-Content -LiteralPath $lock -ErrorAction SilentlyContinue | Select-Object -First 1) -like "$PID *")) { Remove-Item -LiteralPath $lock -Force } }

$harness = Join-Path $PSScriptRoot 'Run-PtHarness.ps1'
$deadline = (Get-Date).AddHours($WaitHours)
# Tried again when the card was taken between this script's wait and the harness's own (the owner's game opened in
# those seconds, 2026-10-12 07:08: a trial build then sat in the game folder for the harness's whole wait): the harness
# gives up after three minutes, main's build goes back, and the wait starts anew.
while ($true) {
"== $Scenario : waiting for a free card $(Get-Date -Format HH:mm:ss)"
$quiet = 0
for ($i = 0; (Get-Date) -lt $deadline -and $quiet -lt 3; $i++) {
    if (Test-Path -LiteralPath $stop) { "stopped by the STOP file"; Clear-OwnLock; exit 3 }
    # The lock: taken when nobody alive holds it (another queued run goes first).
    $holder = 0
    if (Test-Path -LiteralPath $lock) { [int]::TryParse(((Get-Content -LiteralPath $lock -ErrorAction SilentlyContinue | Select-Object -First 1) -split ' ')[0], [ref]$holder) | Out-Null }
    $held = $holder -ne 0 -and $holder -ne $PID -and (Get-Process -Id $holder -ErrorAction SilentlyContinue)
    if (-not $held -and $holder -ne $PID) { Set-Content -LiteralPath $lock -Value "$PID $Tag-waiting $([DateTime]::Now.ToString('s'))" -Encoding ASCII }
    # Our game, an offline test (one that is still making its passes shows no GPU use) or a game of the owner's: wait.
    # Run-PtHarness.ps1's own gate watches every other program afterwards.
    $busy = @(Get-Process -Name tgpt_gpu_test, Starfield, 'Fall of Avalon' -ErrorAction SilentlyContinue).Count
    if ($busy -eq 0 -and -not $held) { $quiet++ } else { $quiet = 0 }
    Start-Sleep -Seconds 3
}
if ($quiet -lt 3) { "the card was not free in $WaitHours hours: nothing was started"; Clear-OwnLock; exit 3 }

if ($TrialDll) {
    "== the trial build goes in $(Get-Date -Format HH:mm:ss): $(Get-Short $TrialDll)"
    & (Join-Path $PSScriptRoot 'Deploy-PtBuild.ps1') -Native -NativeFrom $TrialDll -Note "a TRIAL or MEASURING build for ${Scenario}: never left in the game" 2>&1 | Select-Object -Last 2
} else {
    $a = Get-Short $main; $b = Get-Short $installed
    "build\native $a, the game folder $b"
    if ($a -ne $b) { "NOT main's build in the game folder: nothing was started"; Clear-OwnLock; exit 4 }
}
"== run $(Get-Date -Format HH:mm:ss)"
$more = @{}
if ($Measure) { $more['Measure'] = $true }
if ($KeepFrames) { $more['KeepFrames'] = $true }
& $harness -Scenario $Scenario -TimeoutMinutes $TimeoutMinutes -GpuWaitMinutes 3 @more *> $log
$code = $LASTEXITCODE
"run exit $code (its output: $log)"
$notStarted = $code -eq 3 -and (Select-String -LiteralPath $log -Pattern 'Nothing was started' -Quiet) -and -not (Test-Path -LiteralPath $stop)
if (-not $notStarted -or (Get-Date) -ge $deadline) { break }
"the card was taken before the game could start: main's build back, waiting again"
if ($TrialDll) { & (Join-Path $PSScriptRoot 'Deploy-PtBuild.ps1') -Native -NativeFrom $main -Note "main again: a trial run could not start (the card was taken)" 2>&1 | Select-Object -Last 1 }
}
for ($i = 0; $i -lt 30 -and (Get-Process -Name 'Fall of Avalon' -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Seconds 2 }
if ($TrialDll) {
    "== main's build back $(Get-Date -Format HH:mm:ss)"
    & (Join-Path $PSScriptRoot 'Deploy-PtBuild.ps1') -Native -NativeFrom $main -Note "main again after a trial or measuring build" 2>&1 | Select-Object -Last 2
    "build\native $(Get-Short $main), the game folder $(Get-Short $installed)"
}
"config backup pending: $(Test-Path (Join-Path $repo 'artifacts\harness\_config-backup'))"
$r = (Get-ChildItem (Join-Path $repo 'artifacts\harness') -Directory -Filter "2026*" | Sort-Object Name -Descending | Select-Object -First 1).FullName
$r
if (Test-Path "$r\report.md") {
    Select-String -Path "$r\report.md" -Pattern '^## [0-9]+ problems|did not finish|Other programs used|^\| [0-9]+-[a-z0-9-]* \(-\) \|' |
        ForEach-Object { ($_.Line -replace '; structures.*', '') } | ForEach-Object { $_.Substring(0, [Math]::Min(330, $_.Length)) }
}
if (Test-Path "$r\TGPathTracing.Native.log") {
    $starts = @(Select-String -LiteralPath "$r\TGPathTracing.Native.log" -Pattern 'extension bootstrap')
    $from = if ($starts.Count) { $starts[-1].LineNumber } else { 0 }
    Select-String -LiteralPath "$r\TGPathTracing.Native.log" -Pattern 'The GPU stopped|DESCRIPTOR SLOT MISUSE|no DirectX Raytracing 1.2' |
        Where-Object { $_.LineNumber -gt $from } | ForEach-Object { $_.Line.Substring(0, [Math]::Min(1200, $_.Line.Length)) }
}
"== done $(Get-Date -Format HH:mm:ss)"
