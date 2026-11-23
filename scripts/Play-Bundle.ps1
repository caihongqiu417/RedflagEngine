$ErrorActionPreference = 'Stop'
$renderOptions = @()
if ($args.Count -gt 0) {
    if ($args.Count -ne 2 -or $args[0] -cne '--render-size' -or
        $args[1] -cnotin @('1920x1080', '1280x720')) {
        [Console]::Error.WriteLine('Usage: Play.cmd [--render-size 1920x1080|1280x720]')
        exit 2
    }
    $renderOptions = @('--render-size', $args[1])
}

if ($env:REDFLAG_BUNDLE_COMPANION -eq '1') {
    & "$PSScriptRoot\Start-NpcServices.ps1" -RequireAll
}
elseif ((Test-Path '.\Play-With-Companion.cmd') -and (Test-Path '.\CompanionRuntime\python\python.exe')) {
    & "$PSScriptRoot\Start-NpcServices.ps1"
}
$manifest = Get-Content -LiteralPath 'asset-manifest.json' -Raw -Encoding UTF8 | ConvertFrom-Json
& '.\RedflagForge.exe' --vulkan --play $manifest.scene @renderOptions
exit $LASTEXITCODE
