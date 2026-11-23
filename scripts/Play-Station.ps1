param([switch]$Editor)
$ErrorActionPreference = 'Stop'
& "$PSScriptRoot/Start-NpcServices.ps1"
Set-Location -LiteralPath "$PSScriptRoot/../RedflagForge"
$options = if ($Editor) { @('--vulkan') } else { @('--vulkan', '--play', 'assets/scenes/Watt-Station.redflag') }
& '../bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe' @options @args
exit $LASTEXITCODE
