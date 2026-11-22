param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [ValidateRange(1, 128)]
    [int]$Jobs = 6
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$work = Join-Path $root 'tmp/physx-5.6.1'
$install = Join-Path $root 'bin-int/dependencies/PhysX'
$commit = '5ca9f472105a90d70d957c243cb0ef36fe251a9f'
$source = Join-Path $work "PhysX-$commit"
$mode = $Configuration.ToLowerInvariant()
New-Item -Force -ItemType Directory $work | Out-Null

function Get-Archive($File, $Url, $Algorithm, $Hash) {
    $archive = Join-Path $work $File
    if ((Test-Path -LiteralPath $archive) -and (Get-FileHash -LiteralPath $archive -Algorithm $Algorithm).Hash -eq $Hash) {
        return $archive
    }

    $network = Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
    $options = @('--fail', '--silent', '--show-error', '--location', '--retry', '2')
    if ($network.ProxyEnable -eq 1 -and $network.ProxyServer -and $network.ProxyServer -notmatch '=') {
        $options += @('--proxy', $network.ProxyServer)
    }
    $download = "$archive.download"
    & curl.exe @options --output $download $Url
    if ($LASTEXITCODE) { throw "Could not download $Url." }
    if ((Get-FileHash -LiteralPath $download -Algorithm $Algorithm).Hash -ne $Hash) {
        throw "Checksum mismatch for $File."
    }
    Move-Item -LiteralPath $download -Destination $archive -Force
    return $archive
}

function Expand-Package($Archive, $Directory, $Marker, [string[]]$Members) {
    if (Test-Path -LiteralPath $Marker) { return }
    New-Item -ItemType Directory -Force $Directory | Out-Null
    & tar.exe -xf $Archive -C $Directory @Members
    if ($LASTEXITCODE) { throw "Could not extract $Archive." }
    New-Item -ItemType File -Force $Marker | Out-Null
}

# NVIDIA tag 107.3-physx-5.6.1. Its Windows CMake files copy freeglut even when the sample programs are disabled.
$archive = Get-Archive 'source.zip' "https://codeload.github.com/NVIDIA-Omniverse/PhysX/zip/$commit" SHA256 'b20b5c497ae51e6556519323a999447adf1c24a8b6265ecaee23ac40fb62be12'
Expand-Package $archive $work "$source/.unpacked" @("PhysX-$commit/physx", "PhysX-$commit/LICENSE.md")
$env:PM_freeglut_PATH = Join-Path $work 'freeglut'
$glut = Get-Archive 'freeglut.7z' 'https://d4i3qtqj3r0z5.cloudfront.net/freeglut-windows%403.4_1.1.7z' SHA512 'c01cb75dd466d6889a72d7236669bfce841cc6da9e0edb4208c4affb5ca939f28d64bc3d988bc85d98c589b0b42ac3464f606c89f6c113106669fc9fe84000e5'
Expand-Package $glut $env:PM_freeglut_PATH "$env:PM_freeglut_PATH/.unpacked"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
if (!$vs) { throw 'Install Visual Studio 2022 with x64 C++ build tools and CMake tools for Windows.' }
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:VSLANG = '1033'

# The SDK libraries are static. The MSVC runtime matches Redflag: /MDd for Debug, /MD for Release.
$sdk = "$source/physx".Replace('\', '/')
$output = $work.Replace('\', '/')
$build = "$work/build-$mode"
$options = @('-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$mode", '-DTARGET_BUILD_PLATFORM=windows', '-DPX_OUTPUT_ARCH=x86',
    "-DPHYSX_ROOT_DIR=$sdk", "-DPX_OUTPUT_LIB_DIR=$output", "-DPX_OUTPUT_BIN_DIR=$output",
    '-DNV_USE_STATIC_WINCRT=OFF', '-DNV_USE_DEBUG_WINCRT=ON', '-DPX_GENERATE_STATIC_LIBRARIES=ON',
    '-DPX_BUILDSNIPPETS=OFF', '-DPX_BUILDPVDRUNTIME=OFF', '-DPX_GENERATE_GPU_PROJECTS=OFF')
& cmake -S "$sdk/compiler/public" -B $build @options
if ($LASTEXITCODE) { throw 'PhysX CMake configuration failed.' }
& cmake --build $build --target PhysXExtensions PhysXCooking --parallel $Jobs
if ($LASTEXITCODE) { throw "PhysX $Configuration build failed." }

$lib = Join-Path $install "lib/$mode"
New-Item -ItemType Directory -Force -Path $lib | Out-Null
Copy-Item -LiteralPath "$sdk/include" -Destination $install -Recurse -Force
Copy-Item -LiteralPath "$source/LICENSE.md" -Destination $install -Force
foreach ($name in 'PhysX', 'PhysXCommon', 'PhysXFoundation', 'PhysXExtensions', 'PhysXPvdSDK', 'PhysXCooking') {
    Copy-Item -LiteralPath "$work/bin/win.x86_64.vc143.md/$mode/${name}_static_64.lib" -Destination $lib -Force
}
Write-Output "PhysX 5.6.1 $Configuration x64 is ready at $install."
