@echo off
rem The worker's build of the plugin, into the copy's own bin folder. It reads the game's assemblies (Build.Local.props
rem names the game folder) and writes nothing there: DeployToGame stays false.
setlocal
set "DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE=true"
set "MSBuildEnableWorkloadResolver=false"
dotnet build "C:\Users\Frede\AppData\Local\Temp\tg-agents\wt\psr\src\TGPathTracing\TGPathTracing.csproj" -c Release -p:DeployToGame=false -nologo
exit /b %errorlevel%
