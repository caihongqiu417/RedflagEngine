# 在 Windows 编译 Redflag

RedflagForge 使用 Visual Studio 2022 的 C++17 编译器。请在 Visual Studio Installer 中选择“使用 C++ 的桌面开发”、MSVC v143、Windows SDK 和 .NET Framework 4.7.2 开发工具包。C# 脚本工程使用这个版本的目标框架，编辑器运行时由 Mono 加载它们。[Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/) · [.NET Framework 4.7.2 Developer Pack](https://dotnet.microsoft.com/en-us/download/dotnet-framework/net472)

安装 Windows x64 Vulkan SDK 和 Mono 6.12 x64 SDK。此次独立构建使用 Vulkan SDK 1.3.275.0，Debug 配置会链接 shaderc 和 SPIRV-Cross 的调试库。安装 Vulkan SDK 时勾选调试库；安装后重新打开终端，让 `VULKAN_SDK` 指向 SDK 目录。Mono 默认装在 `C:\Program Files\Mono`。若选择了其他位置，把 `REDFLAG_MONO_SDK` 设为包含 `bin` 和 `lib` 的安装目录。运行编辑器需要的托管程序集随仓库提供。[LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows) · [Mono Windows x64 SDK](https://www.mono-project.com/download/stable/)

在准备存放源码的目录运行下面的命令。Git 按主仓库记录的版本下载各个子模块。第一次生成工程时，脚本会取得 Premake 5.0.0-beta2 的官方 Windows 发布包并校验 SHA-256。已经检出的子模块可以保留自己的工作分支和未提交改动。[Premake 固定版本](https://github.com/premake/premake-core/releases/tag/v5.0.0-beta2)

```bat
git clone --recurse-submodules https://github.com/caihongqiu417/RedflagEngine.git
cd RedflagEngine
GenerateProjects.bat
```

用 Visual Studio 打开生成的 `Redflag.sln`，选择 `Debug | x64` 后生成解决方案。也可以在 VS 2022 的 x64 开发者命令提示符中，从仓库根目录运行以下命令。

```bat
msbuild Redflag.sln /m /p:Configuration=Debug /p:Platform=x64
Open-Vulkan-Editor.cmd
```

编辑器会生成在 `bin\Debug-windows-x86_64\RedflagForge`。Assimp 的材质修正和 Dear ImGui 的窗口布局修正应用在 `bin-int\dependencies` 下的编译副本。FBX 导入和 zlib 的源文件由根目录的 `premake5.lua` 加入工程。引擎原先的本地 Mono 导入库与 DLL 仍可使用；新检出会从 Mono SDK 链接导入库，并将对应的 DLL 复制到编辑器目录。
