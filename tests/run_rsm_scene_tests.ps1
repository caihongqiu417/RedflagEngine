param([string]$Output = 'tmp/rsm-diagnostics/current')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$previous = Get-Location
try {
    Set-Location -LiteralPath $root
    python tests/prepare_rsm_scenes.py $Output
    if ($LASTEXITCODE -ne 0) { throw 'RSM configuration generation failed' }
    $directory = (Resolve-Path -LiteralPath $Output).Path
    $configs = Get-ChildItem -LiteralPath $directory -Filter '*.yaml' | Sort-Object Name
    & tests/run_scene_gallery.cmd $configs[0].FullName *> (Join-Path $directory ($configs[0].BaseName + '.log'))
    if ($LASTEXITCODE -ne 0) { throw ($configs[0].Name + ' failed') }
    $env:PATH = "$root/bin/Debug-windows-x86_64/RedflagForge;$env:PATH"
    Set-Location -LiteralPath (Join-Path $root 'RedflagForge')
    foreach ($config in $configs | Select-Object -Skip 1) {
        & ../bin-int/Tests/SceneGallery.exe $config.FullName *> (Join-Path $directory ($config.BaseName + '.log'))
        if ($LASTEXITCODE -ne 0) { throw ($config.Name + ' failed') }
    }
    Set-Location -LiteralPath $root
    python tests/analyze_rsm_scene.py $directory
    if ($LASTEXITCODE -ne 0) { throw 'RSM numeric checks failed' }
}
finally {
    Set-Location -LiteralPath $previous
}
