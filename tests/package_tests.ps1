# SPDX-License-Identifier: MIT
# Filesystem packaging tests with FAKE binaries and mocked binary validation.
# No GPU DLL is loaded, no game is started, and no real game folder is used.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'tools/Package.Common.ps1')
# Deliberately replace platform/binary checks only within this test script's scope.
function Assert-TgfgManagedVersion([string]$Path,[string]$Name,[string]$Version) { Assert-TgfgFile $Path }
function Assert-TgfgAmd64([string]$Path) { Assert-TgfgFile $Path }
function Assert-TgfgNvidiaSignature([string]$Path) { Assert-TgfgFile $Path }
function Assert-TgfgSignature([string]$Path,[string]$Signer) { Assert-TgfgFile $Path }
function Assert-TgfgGameClosed { }
function Assert-TgfgGame([string]$GameDir,[string]$Runtime) { }
$tests=0
function Assert-True([bool]$Condition,[string]$Message) {
    if(-not $Condition) { throw $Message }; $script:tests++
}
function Assert-Throws([scriptblock]$Action,[string]$Message) {
    $threw=$false; try { & $Action } catch { $threw=$true }
    Assert-True $threw $Message
}
$temp=Join-Path ([IO.Path]::GetTempPath()) ('tgfg-package-tests-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $stage=Join-Path $temp 'stage'; $game=Join-Path $temp 'fake-game'
    New-Item -ItemType Directory -Path $game | Out-Null
    $records=@()
    foreach($relative in (Get-TgfgRequiredPaths)) {
        $path=Get-TgfgOwnedPath $stage $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        Set-Content -LiteralPath $path -Value ('TEST FIXTURE - NOT A BINARY - '+$relative)
        $records += [ordered]@{path=$relative; sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    }
    $manifest=[ordered]@{version='0.5.0';runtime='Mono';framework='net472';native_version='0.5.0';native_abi='0x00050000';files=$records}
    $manifestPath=Join-Path $stage 'TGFG-Package-manifest.json'
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath
    $valid=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Test-TgfgPayload $valid $stage 'Mono'; $tests++
    Assert-Throws { Test-TgfgPayload $valid $stage 'IL2CPP' } 'Wrong runtime was accepted.'
    Assert-Throws { Get-TgfgOwnedPath $stage '../outside.dll' } 'Path traversal was accepted.'
    Assert-Throws { Get-TgfgOwnedPath $stage 'dxgi.dll' } 'A game proxy was accepted as payload.'
    $bad=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $bad.files += $bad.files[0]
    Assert-Throws { Test-TgfgPayload $bad $stage 'Mono' } 'Duplicate payload was accepted.'
    $bad=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $bad.files=@($bad.files | Where-Object { $_.path -notlike '*/TGFG.Autoload.dll' })
    Assert-Throws { Test-TgfgPayload $bad $stage 'Mono' } 'Missing preloader was accepted.'
    # NVIDIA's, AMD's and Intel's license terms require their notices next to their runtimes.
    $bad=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $bad.files=@($bad.files | Where-Object { $_.path -notlike '*/Streamline/LICENSE-NVIDIA-RTX-SDKs.txt' })
    Assert-Throws { Test-TgfgPayload $bad $stage 'Mono' } 'A DLSS Frame Generation runtime without its license notice was accepted.'
    $bad=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $bad.files=@($bad.files | Where-Object { $_.path -notlike '*/XeSS/LICENSE-XeSS.txt' })
    Assert-Throws { Test-TgfgPayload $bad $stage 'Mono' } 'A XeSS runtime without its license notice was accepted.'
    Assert-True (@(Get-TgfgRequiredPaths) -contains 'BepInEx/TGFrameGeneration/Native/FidelityFX/amd_fidelityfx_framegeneration_dx12.dll') 'FSR runtime not packaged.'
    $file=Get-TgfgOwnedPath $stage $valid.files[0].path
    $original=[IO.File]::ReadAllBytes($file)
    Add-Content -LiteralPath $file -Value 'tampered'
    Assert-Throws { Test-TgfgPayload $valid $stage 'Mono' } 'Tampered payload was accepted.'
    [IO.File]::WriteAllBytes($file,$original)
    # Existing TGFG file must be backed up; unrelated proxy/config must survive.
    $old=Join-Path $game 'BepInEx/patchers/TGFG.Autoload.dll'
    New-Item -ItemType Directory -Path (Split-Path -Parent $old) -Force | Out-Null
    Set-Content -LiteralPath $old -Value 'old preloader fixture'
    $oldHash=(Get-FileHash -LiteralPath $old -Algorithm SHA256).Hash
    $proxy=Join-Path $game 'dxgi.dll'; Set-Content -LiteralPath $proxy -Value 'unrelated proxy fixture'
    $proxyHash=(Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash
    # The interposer an earlier version installed under NVIDIA's name is backed up and removed.
    $obsolete=Join-Path $game 'BepInEx/TGFrameGeneration/Native/Streamline/sl.interposer.dll'
    New-Item -ItemType Directory -Path (Split-Path -Parent $obsolete) -Force | Out-Null
    Set-Content -LiteralPath $obsolete -Value 'old interposer fixture'
    $obsoleteHash=(Get-FileHash -LiteralPath $obsolete -Algorithm SHA256).Hash
    Install-TgfgPayload $stage $game 'Mono'
    Assert-True ((Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash -eq $proxyHash) 'Proxy changed.'
    Assert-True (-not (Test-Path -LiteralPath $obsolete)) 'Obsolete sl.interposer.dll was not removed.'
    Assert-True (Test-Path -LiteralPath (Join-Path $game 'BepInEx/TGFrameGeneration/Install-manifest.json')) 'Installed manifest missing.'
    $backupFiles=@(Get-ChildItem -LiteralPath (Join-Path $game 'BepInEx/TGFrameGeneration/Backups') -Recurse -File)
    Assert-True (@($backupFiles | Where-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash -eq $oldHash }).Count -eq 1) 'Old payload was not backed up.'
    Assert-True (@($backupFiles | Where-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash -eq $obsoleteHash }).Count -eq 1) 'Obsolete runtime file was not backed up.'
    Test-TgfgPayload $valid $game 'Mono'; $tests++
    Write-Host "$tests mocked filesystem package checks passed. Not a binary, signature, Windows startup, or GPU test."
} finally {
    # This is our unique test directory, never a user-supplied game path.
    Remove-Item -LiteralPath $temp -Recurse -Force
}
