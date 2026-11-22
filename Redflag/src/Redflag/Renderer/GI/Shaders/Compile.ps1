param([string]$VulkanSDK=$env:VULKAN_SDK)
$compiler = if ($VulkanSDK) { Join-Path $VulkanSDK 'Bin/glslc.exe' } else { (Get-Command glslc).Source }
function Compile([string]$source, [string]$output, [string[]]$defines = @()) {
    & $compiler '--target-env=vulkan1.1' '-O' '-mfmt=c' @defines (Join-Path $PSScriptRoot "$source.comp") '-o' (Join-Path $PSScriptRoot "$output.comp.inc")
    if ($LASTEXITCODE) { throw "$output 编译失败" }
}

Compile 'GI' 'GI'
Compile 'GI' 'GIEmission' @('-DRF_EMISSION')
Compile 'GI' 'GIAlphaMask' @('-DRF_ALPHA_MASK')
Compile 'GI' 'GIEmissionAlphaMask' @('-DRF_EMISSION', '-DRF_ALPHA_MASK')
Compile 'Fog' 'Fog'
Compile 'Fog' 'FogAlphaMask' @('-DRF_ALPHA_MASK')
