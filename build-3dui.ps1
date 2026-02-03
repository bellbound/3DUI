# 3DUI Build Script - delegates to main build script
param(
    [ValidateSet("Release", "Debug")][string]$Config = "Release",
    [switch]$Clean,
    [switch]$Reconfigure
)
$mainScript = Join-Path $PSScriptRoot "..\..\build-skse-mods.ps1"
& $mainScript --mod 3DUI -Config $Config -Clean:$Clean -Reconfigure:$Reconfigure
exit $LASTEXITCODE
