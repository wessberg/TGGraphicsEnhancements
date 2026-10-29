# SPDX-License-Identifier: MIT
# One GPT-6-astra WORKER (not a reviewer) through `codex exec`: it does a job in its own copy of the tree
# (docs/AGENT-WORKERS.md; the owner, 2026-10-07: "You can get gpt-6-astra to work on some of the other tasks in parallel
# via codex, so you don't waste claude tokens"). The script returns when the worker ends, so start it in the background.
#
#   .\tools\agents\Start-CodexWorker.ps1 -Name water -Brief <brief.md> -Base base01 [-Note <note.md>] [-Root <folder>]
#   .\tools\agents\Start-CodexWorker.ps1 -Name design-fog -Brief <brief.md> -ReadOnly
#
#   -Name      a code job: the tree copy <Root>\wt\<Name> (tools\agents\New-TreeCopies.ps1 makes it). With -ReadOnly: a
#              label only; the worker reads the main tree and its last message is the document.
#   -Brief     the job, a Markdown file. COPYNAME and BASENAME in it become -Name and -Base.
#   -Note      text put before the brief: what an earlier, stopped worker left in the copy.
#   -Root      where the copies live (default %TEMP%\tg-agents, as New-TreeCopies.ps1).
#   -Effort    the reasoning effort; xhigh always (the owner, 2026-10-07: "I want them at xhigh reasoning").
#
# What comes out: <Root>\codex\<Name>\stdout.log (the console; its first lines name the model, the sandbox and the
# reasoning effort: check them), last.md (the final message), prompt.md (what was sent). A rerun keeps the old three as
# *.1, *.2. The worker's report is the file its brief names, inside its copy.
#
# Permissions. `codex exec` never asks (approval: never): what a sandbox forbids just fails. A code worker runs WITHOUT a
# sandbox (`-s danger-full-access`; the owner's choice, 2026-10-07 18:12: "Full access"), because the `workspace-write`
# sandbox refused to run CMake (installed in the user's profile: "Access is denied"), so nothing native could be built
# and no GPU test run. The rules a sandbox would have enforced are in the prompt below instead. -ReadOnly runs in the
# `read-only` sandbox. A running `codex exec` cannot be sent a message: everything it must know is in the brief. To change
# course, stop its codex.exe (find it by its command line) and start again in the same copy with a -Note.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Name,
    [Parameter(Mandatory)][string]$Brief,
    [string]$Base = '',
    [string]$Note = '',
    [string]$Root = (Join-Path $env:TEMP 'tg-agents'),
    [string]$Model = 'gpt-6-astra',
    [string]$Effort = 'xhigh',
    [switch]$ReadOnly
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$codex = (Get-Command codex -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$out = Join-Path $Root "codex\$Name"
New-Item -ItemType Directory -Path $out -Force | Out-Null
foreach ($f in 'stdout.log', 'last.md', 'prompt.md') {
    $p = Join-Path $out $f
    if (Test-Path -LiteralPath $p) { $k = 1; while (Test-Path -LiteralPath "$p.$k") { $k++ }; Move-Item -LiteralPath $p -Destination "$p.$k" }
}
$text = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $Brief)).Replace('COPYNAME', $Name).Replace('BASENAME', $Base)
$noteText = if ($Note) { [IO.File]::ReadAllText((Resolve-Path -LiteralPath $Note)) + "`n" } else { '' }
if ($ReadOnly) {
    $dir = $repo
    $mode = 'read-only'
    $head = @"
You are $Model, run through ``codex exec`` without anyone to answer questions: decide, and give your reasons.
You run read-only in the main tree: you cannot write files. Your LAST MESSAGE is the document itself, complete, in
Markdown (the coordinator saves it); write nothing before it that the document needs.

$noteText
The job:

"@
} else {
    $dir = Join-Path $Root "wt\$Name"
    if (-not (Test-Path -LiteralPath (Join-Path $dir 'AGENTS.md'))) { throw "No tree copy at $dir (make it with tools\agents\New-TreeCopies.ps1)." }
    $mode = 'danger-full-access'
    $tgb = Join-Path $env:TEMP 'tgb'
    New-Item -ItemType Directory -Path $tgb -Force | Out-Null
    $game = 'the game folder (Build.Local.props GameDirMono)'
    $props = Join-Path $repo 'Build.Local.props'
    if ((Test-Path -LiteralPath $props) -and ((Get-Content -LiteralPath $props -Raw) -match '<GameDirMono>([^<]+)</GameDirMono>')) { $game = $Matches[1] }
    $cmake = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    $cmakeText = if ($cmake) { "CMake by its full path (it may not be on PATH in your shell): `"$($cmake.Source)`"" } else { 'CMake' }
    $head = @"
You are $Model, run as a worker through ``codex exec`` without anyone to answer questions: decide, write your reasons
into your report, and carry the job through. Your working directory is your own copy of the tree.

You run WITHOUT a sandbox (the owner allows it so that you can build and run the GPU test), and nothing will ask you
for permission. So these rules are yours to keep, and each is absolute:
- Write only inside your copy ($dir) and inside your own build folders $tgb\$Name-*.
- Never write, move or delete anything in the main tree ($repo): read it freely, and run its
  ``tools\harness\Run-PtHarness.ps1 -GpuCheck``, nothing else of it.
- Never touch $game, never start the game (no exe, no Steam URL, no harness run), never deploy.
- Never commit, push, or run a git command that changes anything. Install nothing, download nothing.
- Never stop a process you did not start yourself. Other workers build and test at the same time.

Building: ``vcvars64.bat`` as AGENTS.md says, then $cmakeText
The configure line is in docs/PROJECT-CONTEXT.md "Build recipes" (point the SDK roots at the main tree's folders, the
build folder at $tgb\$Name-build, and build only the targets your job needs, for path tracing TGPathTracing.Native and
tgpt_gpu_test). A plugin: ``dotnet build src/<Mod>/<Mod>.csproj -c Release -f net472 -p:RestoreIgnoreFailedSources=true``.

$noteText
The job:

"@
}
$prompt = Join-Path $out 'prompt.md'
[IO.File]::WriteAllText($prompt, $head + $text, (New-Object Text.UTF8Encoding($false)))
$arguments = "exec -m $Model -s $mode -c model_reasoning_effort=$Effort -C `"$dir`" --skip-git-repo-check -o `"$out\last.md`" -"
# A worker that redirects DOTNET_CLI_HOME gets .NET's first-run step again, whose banner says it installed an ASP.NET
# HTTPS development certificate (2026-10-07: the store already held it since April; nothing new). Off for workers.
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'; $env:DOTNET_NOLOGO = '1'; $env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
Write-Host "$(Get-Date -Format HH:mm:ss) Codex worker $Name ($Model, $Effort, $mode) in $dir"
# Through cmd, so the prompt reaches codex byte for byte (Windows PowerShell re-encodes what it pipes to a program).
& cmd /c "`"$codex`" $arguments < `"$prompt`" > `"$out\stdout.log`" 2>&1"
Write-Host "$(Get-Date -Format HH:mm:ss) CODEX WORKER $Name DONE (exit $LASTEXITCODE). Last message: $out\last.md"
exit $LASTEXITCODE
