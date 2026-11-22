param([string]$VulkanSDK = $env:VULKAN_SDK)
$ErrorActionPreference = 'Stop'
$compiler = if ($VulkanSDK) { Join-Path $VulkanSDK 'Bin/glslc.exe' } else { (Get-Command glslc).Source }
foreach ($stage in 'vert', 'frag') {
    $source = Join-Path $PSScriptRoot "Mesh.$stage"
    & $compiler '--target-env=vulkan1.1' '-O' '-mfmt=c' $source '-o' "$source.inc"
    if ($LASTEXITCODE -ne 0) { throw "编译失败：$source" }
    & $compiler '--target-env=vulkan1.1' '-DLIGHTING_SURFACES' '-O' '-mfmt=c' $source '-o' (Join-Path $PSScriptRoot "Surface.$stage.inc")
    if ($LASTEXITCODE -ne 0) { throw "编译失败：$source" }
}

$source = Join-Path $PSScriptRoot 'Mesh.frag'
& $compiler '--target-env=vulkan1.1' '-DTRANSPARENT' '-DLIGHTING_SURFACES' '-O' '-mfmt=c' $source '-o' (Join-Path $PSScriptRoot 'Transparency.frag.inc')
if ($LASTEXITCODE -ne 0) { throw "编译失败：$source" }
$source = Join-Path $PSScriptRoot 'Emission.frag'
& $compiler '--target-env=vulkan1.1' '-O' '-mfmt=c' $source '-o' "$source.inc"
if ($LASTEXITCODE -ne 0) { throw "编译失败：$source" }
$source = Join-Path $PSScriptRoot 'Mesh.vert'
& $compiler '--target-env=vulkan1.1' '-DEMISSIVE_UV' '-O' '-mfmt=c' $source '-o' (Join-Path $PSScriptRoot 'Emission.vert.inc')
if ($LASTEXITCODE -ne 0) { throw "编译失败：$source" }
& $compiler '--target-env=vulkan1.1' '-DEMISSIVE_UV' '-DLIGHTING_SURFACES' '-O' '-mfmt=c' $source '-o' (Join-Path $PSScriptRoot 'Transparency.vert.inc')
if ($LASTEXITCODE -ne 0) { throw "编译失败：$source" }
foreach ($shader in 'PackSurfaces.comp', 'CompositeTransparency.comp', 'StoreCascade.comp') {
    $source = Join-Path $PSScriptRoot $shader
    & $compiler '--target-env=vulkan1.1' '-O' '-mfmt=c' $source '-o' "$source.inc"
    if ($LASTEXITCODE -ne 0) { throw "编译失败：$source" }
}
