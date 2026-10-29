# SPDX-License-Identifier: MIT
# Parse the integrated PowerShell scripts without executing their deployment code.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
$paths=@('Build.ps1','Build-Native.ps1','Stage-Runtime.ps1','Package.ps1','Test-Setup.ps1','tools/Package.Common.ps1',
         'tools/Write-NativeManifest.ps1','tools/Test-Scripts.ps1','tests/package_tests.ps1','tools/harness/Run-PtHarness.ps1','tools/harness/Deploy-PtBuild.ps1',
         'tools/harness/Clear-PtHarnessRuns.ps1','tests/harness_cleanup_test.ps1',
         'tools/agents/New-TreeCopies.ps1','tools/agents/Start-CodexWorker.ps1')
$failures=@()
foreach($relative in $paths) {
    $path=Join-Path $root $relative
    $tokens=$null; $errors=$null
    $null=[System.Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
    foreach($errorItem in @($errors)) { $failures += "$relative : $($errorItem.Message) at $($errorItem.Extent.StartLineNumber)" }
}
if($failures.Count -gt 0) { throw ($failures -join [Environment]::NewLine) }
Write-Host "Parsed $($paths.Count) PowerShell scripts. This checks syntax, not Windows/Steam/rendering behavior."
