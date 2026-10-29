# Exercise the installed manager's metadata reader against the bridge's actual tag type.
# This does not open a game window or initialize a Unity component.
param([Parameter(Mandatory=$true)][string]$ManagerDll, [string]$GameDir)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $GameDir) { $GameDir = ([xml](Get-Content -LiteralPath (Join-Path $repo 'Build.Local.props') -Raw)).Project.PropertyGroup.GameDirMono }
foreach ($dependency in @('Fall of Avalon_Data/Managed/UnityEngine.CoreModule.dll', 'Fall of Avalon_Data/Managed/UnityEngine.dll', 'BepInEx/core/BepInEx.dll')) {
    [void][Reflection.Assembly]::LoadFrom((Join-Path $GameDir $dependency))
}
Add-Type -Path (Join-Path $repo 'src/TGGraphicsBridge/ConfigurationManagerAttributes.cs')
$assembly = [Reflection.Assembly]::LoadFrom((Resolve-Path -LiteralPath $ManagerDll).Path)
$metadata = $assembly.GetType('FoAModManager.Plugin+SettingUiMetadata', $true)
$from = $metadata.GetMethod('From', [Reflection.BindingFlags]'Public,NonPublic,Static')
if (-not $from) { throw 'Installed manager does not expose the expected metadata reader.' }
function Read-Metadata($tag) {
    $tags = [Collections.Generic.List[object]]::new()
    $tags.Add($tag.PSObject.BaseObject)
    $arguments = [object[]]::new(1)
    $arguments[0] = $tags
    $result = $from.Invoke($null, $arguments)
    $values = @{}
    foreach ($property in $metadata.GetProperties([Reflection.BindingFlags]'Public,NonPublic,Instance')) {
        $values[$property.Name] = $property.GetValue($result)
    }
    [pscustomobject]$values
}
function Assert-Equal($actual, $expected, [string]$name) {
    if ($actual -cne $expected) { throw "$name expected '$expected', found '$actual'." }
}
$tag = [TGGraphicsBridge.ConfigurationManagerAttributes]::new()
$tag.DisplayName = 'Style'
$tag.DispName = 'Style'
$tag.DisplaySection = 'Video'
$tag.Category = 'Video'
$tag.SortOrder = 5
$tag.ChoiceLabels = 'A=Default;B=A;C=B;D=C'
foreach ($visible in @($false, $true, $false)) {
    $tag.Visible = $visible
    $read = Read-Metadata $tag
    Assert-Equal $read.DisplayName 'Style' 'Display name'
    Assert-Equal $read.DisplaySection 'Video' 'Display section'
    Assert-Equal $read.Order 5 'Row order'
    Assert-Equal $read.Hidden (-not $visible) 'Dynamic visibility'
    foreach ($pair in @(@('A','Default'), @('B','A'), @('C','B'), @('D','C'))) {
        Assert-Equal $read.ChoiceLabels[$pair[0]] $pair[1] 'Style label'
    }
}
$tag.DisplayName = $tag.DispName = 'Neural Rendering'
$tag.ChoiceLabels = 'Off=Off;On=DLSS'
$read = Read-Metadata $tag
Assert-Equal $read.ChoiceLabels['Off'] 'Off' 'Off label'
Assert-Equal $read.ChoiceLabels['On'] 'DLSS' 'On label'
# Mutate only this test tag. The same assertions must notice the broken mapping and visibility.
$tag.ChoiceLabels = 'Off=Off;On=On'
$tag.Visible = $true
$read = Read-Metadata $tag
$rejected = 0
try { Assert-Equal $read.ChoiceLabels['On'] 'DLSS' 'Mutated label' } catch { $rejected++ }
try { Assert-Equal $read.Hidden $true 'Mutated visibility' } catch { $rejected++ }
if ($rejected -ne 2) { throw 'The assertions missed a deliberately broken metadata tag.' }
Write-Host 'PASS: installed FoA metadata reader accepts names, order, dynamic visibility and Default/A/B/C labels. Both broken tags rejected.'
