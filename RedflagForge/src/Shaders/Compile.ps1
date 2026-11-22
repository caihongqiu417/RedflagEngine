param([string]$VulkanSDK = $env:VULKAN_SDK)
$ErrorActionPreference = 'Stop'
$compiler = if ($VulkanSDK) { Join-Path $VulkanSDK 'Bin/glslc.exe' } else { (Get-Command glslc).Source }
foreach ($shader in Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.comp') {
    & $compiler '--target-env=vulkan1.1' '-O' '-mfmt=c' $shader.FullName '-o' ($shader.FullName + '.inc')
    if ($LASTEXITCODE -ne 0) { throw "编译失败：$($shader.Name)" }
}
