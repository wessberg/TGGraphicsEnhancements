# Stop offline tgpt_gpu_test processes when any listed game opens, and continue watching. Run this watchdog
# while workers may test: the pre-launch GPU gate cannot stop a test already running.
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
