param([string] $VulkanSDK = $env:VULKAN_SDK)

$compiler = if ($VulkanSDK) { Join-Path $VulkanSDK 'Bin/glslc.exe' } else { (Get-Command glslc).Source }
function Compile([string]$source, [string]$output, [string[]]$defines = @()) {
    & $compiler '--target-env=vulkan1.1' '-O' '-mfmt=c' "-I$PSScriptRoot" @defines (Join-Path $PSScriptRoot "$source.comp") '-o' (Join-Path $PSScriptRoot "$output.comp.inc")
    if ($LASTEXITCODE) { throw "$output 编译失败" }
}

foreach ($source in Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.comp') {
    Compile $source.BaseName $source.BaseName
}

Compile 'StochasticSSR' 'StochasticSceneSSR' @('-DRF_SCENE_REFLECTION')
Compile 'StochasticSSR' 'StochasticSceneSSRAlphaMask' @('-DRF_SCENE_REFLECTION', '-DRF_ALPHA_MASK')
Compile 'ClusterShade' 'ClusterShadeAlphaMask' @('-DRF_ALPHA_MASK')
