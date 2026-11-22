$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)

# 已有的子模块可能有本地改动。只下载这次检出缺少的目录。
$modules = git config -f .gitmodules --get-regexp '\.path$'
if ($LASTEXITCODE) { throw '无法读取 .gitmodules。请从 Git 工作目录生成工程。' }
$missing = @($modules | ForEach-Object { ($_ -split '\s+', 2)[1] } | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $_ '.git'))
})
if ($missing.Count) {
    & git submodule update --init --recursive -- $missing
    if ($LASTEXITCODE) { throw 'Git 子模块下载失败。解决上面的网络或路径错误后可以重新运行。' }
}

if (-not $env:VULKAN_SDK) {
    $path = 'C:\VulkanSDK\1.3.275.0'
    if (Test-Path -LiteralPath $path) { $env:VULKAN_SDK = $path }
}
if (-not $env:VULKAN_SDK) { throw '请安装 Vulkan SDK x64，重新打开终端，确认 VULKAN_SDK 指向安装目录。' }
foreach ($library in 'shaderc_shared', 'shaderc_sharedd', 'spirv-cross-core', 'spirv-cross-cored', 'spirv-cross-glsl', 'spirv-cross-glsld') {
    if (-not (Test-Path -LiteralPath (Join-Path $env:VULKAN_SDK "Lib\$library.lib"))) {
        throw "Vulkan SDK 缺少 $library.lib，请安装 x64 开发库及 Debug 组件。"
    }
}

$premake = Join-Path $PWD 'vendor\bin\premake\premake5.exe'
if (-not (Test-Path -LiteralPath $premake)) {
    $cache = Join-Path $PWD 'bin-int\tools\premake-5.0.0-beta2'
    New-Item -ItemType Directory -Force -Path $cache | Out-Null
    $archive = Join-Path $cache 'premake.zip'
    $url = 'https://github.com/premake/premake-core/releases/download/v5.0.0-beta2/premake-5.0.0-beta2-windows.zip'
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing
    $expected = '87cfa10ed52fd1f4e835f738ac1033ff302035758671400fec078b700c622c54'
    $hasher = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($archive)
    try { $hash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') }
    finally { $stream.Dispose(); $hasher.Dispose() }
    if ($hash -ne $expected) { throw 'Premake 压缩包校验失败。' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    $tool = Join-Path $cache 'premake5.exe'
    try { [IO.Compression.ZipFileExtensions]::ExtractToFile($zip.GetEntry('premake5.exe'), $tool, $true) }
    finally { $zip.Dispose() }
    New-Item -ItemType Directory -Force -Path (Split-Path $premake -Parent) | Out-Null
    Copy-Item -LiteralPath $tool -Destination $premake
}
& $premake vs2022
exit $LASTEXITCODE
