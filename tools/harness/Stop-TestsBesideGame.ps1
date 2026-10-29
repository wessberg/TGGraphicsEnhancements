# Ends every offline GPU test (tgpt_gpu_test) the moment a game of the owner's opens, and goes on watching.
#
#   .\tools\harness\Stop-TestsBesideGame.ps1 [-UntilHour 9]      started in the background by the coordinator while
#                                                                workers may run offline GPU tests
#
# The owner's rule since 2026-10-12 00:20: no offline GPU test while a game of theirs is open (a worker's test hung
# the card beside Starfield, Windows reset the driver and the game went down). `Run-PtHarness.ps1 -GpuCheck` says no
# BEFORE a test starts; it cannot end a test that is already running when the game opens. That night at 04:51 the
# owner opened Starfield one and a half minutes after three workers' tests had started: one worker's script ended its
# test at once, another's ran on beside the game for minutes. This script ends them all, whatever a worker's own
# script does. A test ended here leaves no result: its runner starts it again when -GpuCheck allows.
# The games are Run-PtHarness.ps1's own list ($ownerGames). It ends by itself at -UntilHour o'clock, or when a file
# STOP-watch stands in artifacts/harness. Each stop is a line on its output and in artifacts/harness/tests-stopped.log.
[CmdletBinding()]
param([int]$UntilHour = 9, [int]$Seconds = 2)
$ErrorActionPreference = 'Continue'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$games = @('Starfield')
$line = Select-String -LiteralPath (Join-Path $PSScriptRoot 'Run-PtHarness.ps1') -Pattern '^\$ownerGames\s*=\s*@\((.+)\)' | Select-Object -First 1
if ($line) { $games = @($line.Matches[0].Groups[1].Value -split ',' | ForEach-Object { $_.Trim().Trim("'").Trim('"') } | Where-Object { $_ }) }
$stop = Join-Path $repo 'artifacts\harness\STOP-watch'
$log = Join-Path $repo 'artifacts\harness\tests-stopped.log'
"watching for $($games -join ', ') until $UntilHour o'clock; $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$stopped = 0
while ((Get-Date).Hour -ne $UntilHour -and -not (Test-Path -LiteralPath $stop)) {
    $open = $null
    foreach ($g in $games) { if (Get-Process -Name $g -ErrorAction SilentlyContinue) { $open = $g; break } }
    if ($open) {
        foreach ($t in @(Get-Process -Name tgpt_gpu_test -ErrorAction SilentlyContinue)) {
            $what = ''
            try { $what = (Get-CimInstance Win32_Process -Filter "ProcessId=$($t.Id)").CommandLine } catch { }
            try { $t | Stop-Process -Force -ErrorAction Stop; $stopped++
                $text = "{0} {1} is open: ended the offline test {2} (started {3}): {4}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $open, $t.Id, $t.StartTime.ToString('HH:mm:ss'), ("$what".Substring(0, [Math]::Min(160, "$what".Length)))
                $text; Add-Content -LiteralPath $log -Value $text -Encoding UTF8
            } catch { }
        }
    }
    Start-Sleep -Seconds $Seconds
}
"ended $(Get-Date -Format HH:mm:ss); $stopped tests stopped"
