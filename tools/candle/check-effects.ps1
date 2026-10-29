$ErrorActionPreference = 'Stop'
$env:DOTNET_CLI_HOME = Join-Path $PSScriptRoot 'dotnet'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$sdk = Join-Path $env:ProgramFiles 'dotnet/sdk/10.0.201/Roslyn/bincore/csc.dll'
$refs = Join-Path $env:USERPROFILE '.nuget/packages/microsoft.netframework.referenceassemblies.net472/1.0.3/build/.NETFramework/v4.7.2'
$output = Join-Path $PSScriptRoot 'effects-test.exe'
# Existing compiler and reference cache only; no restore or download.
& dotnet $sdk /nologo /noconfig /nostdlib+ /target:exe "/out:$output" "/r:$refs/mscorlib.dll" "/r:$refs/System.dll" "/r:$refs/System.Core.dll" "$root/tests/candle_effects_test.cs" "$root/src/TGPathTracing/EffectPasses.cs"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $output
exit $LASTEXITCODE
