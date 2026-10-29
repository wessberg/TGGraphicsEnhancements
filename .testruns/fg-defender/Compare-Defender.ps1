$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$scanner = 'C:\ProgramData\Microsoft\Windows Defender\Platform\4.18.26080.4-0\MpCmdRun.exe'
$game = 'C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA'
$cases = @(
    @{ Name='FG previous public Mono 1.2.0'; Path=(Join-Path $repo 'artifacts\Packages\TGFrameGeneration-1.2.0-Mono-20260928-002619-676-42fe2e33\BepInEx\TGFrameGeneration\Native\TGFG.Native.dll') },
    @{ Name='FG October 7, before community support'; Path=(Join-Path $repo 'build\nr\install\TGFrameGeneration\TGFG.Native.dll') },
    @{ Name='FG October 9 release 1.2.3'; Path=(Join-Path $repo 'build\nr\mono-r17\verified\TGFrameGeneration-20261009-114849-173\BepInEx\TGFrameGeneration\Native\TGFG.Native.dll') },
    @{ Name='Better Upscaling installed release'; Path=(Join-Path $game 'BepInEx\TGBetterUpscaling\Native\TGBetterUpscaling.Native.dll') },
    @{ Name='Graphics Bridge installed release'; Path=(Join-Path $game 'BepInEx\TGBetterUpscaling\Native\TGGraphicsBridge.Native.dll') },
    @{ Name='Neural Rendering installed release'; Path=(Join-Path $game 'BepInEx\TGNeuralRendering\Native\TGNeuralRendering.Native.dll') }
)
$results = foreach ($case in $cases) {
    $started = Get-Date -Format o
    # Per-scan option only: ignore file exclusions and report without remediation.
    # No Defender settings, allow rules, or signatures are changed by this script.
    $lines = & $scanner -Scan -ScanType 3 -File $case.Path -DisableRemediation 2>&1
    $exitCode = $LASTEXITCODE
    [pscustomobject]@{ Name=$case.Name; Path=$case.Path; Started=$started; ExitCode=$exitCode; Output=($lines -join "`n") }
}
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'scan-results.json') -Encoding UTF8
$results | Select-Object Name,ExitCode,Output | Format-List
