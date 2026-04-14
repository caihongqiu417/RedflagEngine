# Redflag C# 脚本系统开发待办 (Mono 嵌入)

## 前置知识（通俗版）

### 为什么需要 Mono？

C++ 引擎无法直接运行 C# 代码。**Mono** 是一个开源的 .NET 运行时，可以作为 C 库嵌入到 C++ 程序中。嵌入后引擎就能：
1. 加载 C# 编译出的 DLL（称为 Assembly）
2. 在 DLL 中找到 C# 类，创建它的对象
3. 调用 C# 对象的方法（如 OnCreate、OnUpdate）
4. C# 反过来也能调用 C++ 函数（通过 InternalCall 注册）

### 整体架构

```
C++ 引擎                    Mono 运行时                  C# 脚本
──────────                  ──────────                  ──────
1. 初始化Mono         →    创建JIT虚拟机
2. 加载DLL           →    解析Assembly             ←  dotnet build 编译出的DLL
3. 找到类"Player"     →    mono_class_from_name     ←  public class Player : Entity
4. 创建实例           →    mono_object_new          ←  new Player()
5. 调用OnUpdate       →    mono_runtime_invoke      ←  Player.OnUpdate(ts)
6. C#读写Transform    ←    InternalCall桥梁    →      Entity_SetTranslation(C++函数)
```

### 关键概念

- **InternalCall**：C# 声明"我的实现在 C++ 那边"，Mono 负责桥接调用。这是 C# 操控 C++ 引擎的唯一通道
- **Assembly**：C# 编译出的 DLL 文件。需要两个——引擎基础类库 `Redflag-ScriptCore.dll` + 用户游戏脚本 `GameScripts.dll`
- **GCHandle**：Mono 有垃圾回收，C++ 侧必须用 GCHandle "钉住"对象防止被回收
- **ScriptClass**：C++ 对 `MonoClass*` 的封装，代表一个 C# 类的**类型信息**
- **ScriptInstance**：C++ 对 `MonoObject*` 的封装，代表一个活着的 C# **对象实例**

### 运行时每帧数据流

```
Scene::OnUpdateRuntime(ts)
  ├─ 遍历所有有 ScriptComponent 的实体
  │   ├─ 如果还没创建 C# 实例 → ScriptEngine::CreateInstance("Player")
  │   │     └─ Mono: 分配对象 → 调用构造函数 → 存储 GCHandle
  │   ├─ 调用 C# 的 OnUpdate(ts)
  │   │     └─ C# 代码中: transform.Translation = new Vec3(x, y, z);
  │   │           └─ 这其实调用了 InternalCall → C++ 侧直接写 TransformComponent
  │   └─ 继续到下一个实体
  ├─ Box2D 物理更新
  └─ 渲染
```

### 前置要求

- [ ] 下载 Mono 预编译库（推荐从 [Hazel 仓库](https://github.com/TheCherno/Hazel) 的 vendor/mono 中获取打包好的版本）
- [ ] 安装 .NET SDK（用于编译 C# 项目，命令行 `dotnet build`）

---

## 阶段一：集成 Mono 运行时到构建系统

**目标**：让引擎能编译并链接 Mono 库，初始化/关闭 Mono 运行时

**里程碑**：启动应用时日志输出 "Mono JIT runtime created" ✓

- [ ] **1.1 放置 Mono SDK 文件**
  - 在 `Redflag/vendor/mono/` 下放入：
    - `include/` — Mono 头文件（`mono/jit/jit.h`、`mono/metadata/assembly.h` 等）
    - `lib/Debug/mono-2.0-sgen.lib` — Debug 链接库
    - `lib/Release/mono-2.0-sgen.lib` — Release 链接库
    - `bin/mono-2.0-sgen.dll` — 运行时 DLL
  - 来源：从 Hazel 仓库 `vendor/mono` 目录复制，或从 Mono 官网下载

- [ ] **1.2 修改 `premake5.lua` — 添加 Mono 依赖**
  - 在 `IncludeDir` 表中添加：
    ```lua
    IncludeDir["mono"] = "Redflag/vendor/mono/include"
    ```
  - 在 Redflag 项目的 `includedirs` 中添加：
    ```lua
    "%{IncludeDir.mono}",
    ```
  - 在 Redflag 项目中按配置添加 `libdirs`：
    ```lua
    filter "configurations:Debug"
        libdirs { "Redflag/vendor/mono/lib/Debug" }
    filter "configurations:Release"
        libdirs { "Redflag/vendor/mono/lib/Release" }
    ```
  - 在 Redflag 项目的 `links` 中添加：
    ```lua
    "mono-2.0-sgen",
    ```
  - 在 Redflag 项目的 `postbuildcommands` 中复制 DLL 到输出目录：
    ```lua
    "{COPY} ../Redflag/vendor/mono/bin/mono-2.0-sgen.dll ../bin/" .. outputdir .. "/RedflagForge",
    ```

- [ ] **1.3 新建 `Redflag/src/Redflag/Scripting/ScriptEngine.h`**
  - 静态类 `ScriptEngine`
  - 公共接口：
    - `static void Init()` — 初始化 Mono 运行时
    - `static void Shutdown()` — 释放 Mono 运行时
  - `Init()` 实现要点：
    - `mono_set_assemblies_path("mono/lib")` — 告诉 Mono 去哪里找基础类库
    - `MonoDomain* rootDomain = mono_jit_init("RedflagJITRuntime")` — 创建根域
    - 创建一个 App Domain：`mono_domain_create_appdomain("RedflagScriptRuntime", nullptr)`
    - `mono_domain_set(appDomain, true)` — 设为当前域
  - `Shutdown()` 实现要点：
    - `mono_domain_set(mono_get_root_domain(), false)`
    - `mono_domain_unload(appDomain)`
    - `mono_jit_cleanup(rootDomain)`

- [ ] **1.4 新建 `Redflag/src/Redflag/Scripting/ScriptEngine.cpp`**
  - 实现上述 Init/Shutdown 逻辑
  - 在 Init 末尾添加日志：`RF_CORE_INFO("ScriptEngine: Mono JIT runtime created");`
  - 注意：此文件需要 `#include "rfpch.h"` 因为 Redflag 项目使用预编译头

- [ ] **1.5 修改 `Redflag/src/Redflag/Core/Application.cpp` — 调用 Init/Shutdown**
  - 在 `Application` 构造函数中，`Renderer::Init()` 之后添加：
    ```cpp
    ScriptEngine::Init();
    ```
  - 在 `Application` 析构函数中添加：
    ```cpp
    ScriptEngine::Shutdown();
    ```
  - 添加头文件：`#include "Redflag/Scripting/ScriptEngine.h"`

- [ ] **1.6 重新生成项目并编译验证**
  - 运行 `GenerateProjects.bat` 重新生成 .sln
  - 编译 RedflagForge
  - 启动后检查日志是否输出 "Mono JIT runtime created"

---

## 阶段二：加载 C# Assembly 并调用方法

**目标**：能加载一个 C# DLL，找到里面的类，创建实例，调用方法

**里程碑**：加载测试 DLL，调用 C# 的 `PrintMessage()`，C++ 控制台输出结果 ✓

- [ ] **2.1 ScriptEngine 添加 Assembly 加载功能**
  - 在 `ScriptEngine` 中添加：
    - `static void LoadAssembly(const std::filesystem::path& filepath)`
  - 实现步骤：
    1. 用 `std::ifstream` 以二进制模式读取 DLL 文件到 `std::vector<char>`
    2. `MonoImageOpenStatus status;`
    3. `MonoImage* image = mono_image_open_from_data(data.data(), data.size(), true, &status)`
    4. `MonoAssembly* assembly = mono_assembly_load_from_full(image, filepath.string().c_str(), &status, false)`
    5. `mono_image_close(image)` — assembly 会持有自己的 image 引用
  - 存储 `MonoAssembly*` 和通过 `mono_assembly_get_image(assembly)` 获取的 `MonoImage*` 到静态成员

- [ ] **2.2 定义 ScriptClass 类**（在 `ScriptEngine.h` 中）
  - 成员：
    - `std::string m_ClassNamespace` — C# 命名空间（如 "Redflag"）
    - `std::string m_ClassName` — C# 类名（如 "Entity"）
    - `MonoClass* m_MonoClass` — Mono 类指针
  - 构造函数：
    ```cpp
    ScriptClass(const std::string& classNamespace, const std::string& className);
    // 内部调用 mono_class_from_name(image, classNamespace, className)
    ```
  - 方法：
    - `MonoObject* Instantiate()` — `mono_object_new()` + `mono_runtime_object_init()`
    - `MonoMethod* GetMethod(const std::string& name, int paramCount)` — `mono_class_get_method_from_name()`
    - `MonoObject* InvokeMethod(MonoObject* instance, MonoMethod* method, void** params)` — `mono_runtime_invoke()`

- [ ] **2.3 定义 ScriptInstance 类**（在 `ScriptEngine.h` 中）
  - 代表一个活着的 C# 脚本对象实例
  - 成员：
    - `Ref<ScriptClass> m_ScriptClass` — 对应的类
    - `MonoObject* m_Instance` — C# 对象指针
    - `uint32_t m_GCHandle` — GC 句柄（`mono_gchandle_new(instance, false)`）
    - `MonoMethod* m_Constructor` — 含 ulong 参数的构造函数
    - `MonoMethod* m_OnCreateMethod` — OnCreate 方法缓存
    - `MonoMethod* m_OnUpdateMethod` — OnUpdate(float) 方法缓存
  - 构造函数：
    ```cpp
    ScriptInstance(Ref<ScriptClass> scriptClass, Entity entity);
    // 1. 调用 scriptClass->Instantiate() 分配对象
    // 2. mono_gchandle_new() 防止 GC 回收
    // 3. 查找并缓存 OnCreate / OnUpdate 方法
    // 4. 调用构造函数设置 Entity ID
    ```
  - 方法：
    - `void InvokeOnCreate()` — 调用 C# OnCreate
    - `void InvokeOnUpdate(float ts)` — 调用 C# OnUpdate，参数通过 `void* params[] = { &ts }` 传递

- [ ] **2.4 加载两个 Assembly 的机制**
  - `ScriptEngine::Init()` 中：
    1. 先加载 `Redflag-ScriptCore.dll`（引擎基础库）→ 存为 `s_CoreAssembly` / `s_CoreImage`
    2. 再加载 `GameScripts.dll`（用户脚本）→ 存为 `s_AppAssembly` / `s_AppImage`
  - DLL 路径约定：`RedflagForge/assets/scripts/Redflag-ScriptCore.dll` 和 `RedflagForge/assets/scripts/GameScripts.dll`
  - ScriptClass 查找类时，引擎类用 `s_CoreImage`，用户脚本类用 `s_AppImage`

- [ ] **2.5 编写测试验证**
  - 创建一个最简 C# 项目，编译出 DLL，包含一个 `PrintMessage` 静态方法
  - 在 `ScriptEngine::Init()` 末尾临时调用它，验证控制台有输出
  - 验证通过后删除测试代码

---

## 阶段三：创建 C# 侧基础类库 (Redflag-ScriptCore)

**目标**：定义用户脚本需要继承的基类和可用的引擎 API

**里程碑**：`dotnet build` 编译出 `Redflag-ScriptCore.dll` ✓

- [ ] **3.1 创建 C# 项目 `Redflag-ScriptCore/`**
  - 在项目根目录下新建 `Redflag-ScriptCore/` 文件夹
  - 创建 `Redflag-ScriptCore.csproj`：
    ```xml
    <Project Sdk="Microsoft.NET.Sdk">
      <PropertyGroup>
        <TargetFramework>net472</TargetFramework>
        <!-- 或 netstandard2.0，均与 Mono 兼容 -->
        <OutputType>Library</OutputType>
        <RootNamespace>Redflag</RootNamespace>
        <OutputPath>../RedflagForge/assets/scripts/</OutputPath>
        <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
      </PropertyGroup>
    </Project>
    ```

- [ ] **3.2 新建 `Redflag-ScriptCore/Source/Redflag/Vector3.cs`**
  - 定义 `Vector3` 结构体（与 `glm::vec3` 内存布局一致 —— 3 个连续 float）：
    ```csharp
    using System.Runtime.InteropServices;

    namespace Redflag
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct Vector3
        {
            public float X, Y, Z;

            public Vector3(float x, float y, float z) { X = x; Y = y; Z = z; }
            public Vector3(float scalar) { X = Y = Z = scalar; }

            public static Vector3 Zero => new Vector3(0f);

            public static Vector3 operator +(Vector3 a, Vector3 b)
                => new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
            public static Vector3 operator *(Vector3 v, float s)
                => new Vector3(v.X * s, v.Y * s, v.Z * s);
        }
    }
    ```
  - `[StructLayout(LayoutKind.Sequential)]` 确保字段按声明顺序排列，与 C++ 侧 `glm::vec3` 一一对应

- [ ] **3.3 新建 `Redflag-ScriptCore/Source/Redflag/InternalCalls.cs`**
  - 声明所有 C++ 桥梁函数：
    ```csharp
    using System.Runtime.CompilerServices;

    namespace Redflag
    {
        internal static class InternalCalls
        {
            // Transform
            [MethodImpl(MethodImplOptions.InternalCall)]
            internal extern static void Entity_GetTranslation(ulong entityID, out Vector3 translation);

            [MethodImpl(MethodImplOptions.InternalCall)]
            internal extern static void Entity_SetTranslation(ulong entityID, ref Vector3 translation);

            // Input
            [MethodImpl(MethodImplOptions.InternalCall)]
            internal extern static bool Input_IsKeyDown(int keycode);

            // Log
            [MethodImpl(MethodImplOptions.InternalCall)]
            internal extern static void Log_Info(string message);

            [MethodImpl(MethodImplOptions.InternalCall)]
            internal extern static void Log_Warn(string message);
        }
    }
    ```
  - `[MethodImpl(MethodImplOptions.InternalCall)]` 告诉 Mono：这个方法没有 C# 实现体，运行时去找 C++ 注册的同名函数

- [ ] **3.4 新建 `Redflag-ScriptCore/Source/Redflag/Entity.cs`**
  - 这是用户脚本的**基类**（类似 Unity 的 MonoBehaviour）：
    ```csharp
    namespace Redflag
    {
        public class Entity
        {
            public readonly ulong ID;  // 对应 C++ 的 UUID

            protected Entity() { ID = 0; }
            internal Entity(ulong id) { ID = id; }

            public Vector3 Translation
            {
                get { InternalCalls.Entity_GetTranslation(ID, out var t); return t; }
                set { InternalCalls.Entity_SetTranslation(ID, ref value); }
            }

            protected virtual void OnCreate() {}
            protected virtual void OnUpdate(float ts) {}
        }
    }
    ```
  - C++ 侧创建实例后会通过 Mono 反射设置 `ID` 字段为实体的 UUID

- [ ] **3.5 新建 `Redflag-ScriptCore/Source/Redflag/Input.cs`**
  - 对 InternalCall 的友好封装：
    ```csharp
    namespace Redflag
    {
        public static class Input
        {
            public static bool IsKeyDown(KeyCode keycode)
                => InternalCalls.Input_IsKeyDown((int)keycode);
        }
    }
    ```

- [ ] **3.6 新建 `Redflag-ScriptCore/Source/Redflag/KeyCode.cs`**
  - C# 侧的按键枚举，值必须与 C++ 的 `KeyCode.h` 一一对应：
    ```csharp
    namespace Redflag
    {
        public enum KeyCode
        {
            // 从 Redflag/src/Redflag/Core/KeyCode.h 复制数值
            Space = 32,
            A = 65, D = 68, S = 83, W = 87,
            Right = 262, Left = 263, Down = 264, Up = 265,
            LeftShift = 340, LeftControl = 341,
            // ... 其余按需添加
        }
    }
    ```

- [ ] **3.7 编译验证**
  - 在 `Redflag-ScriptCore/` 目录运行 `dotnet build`
  - 确认 `RedflagForge/assets/scripts/Redflag-ScriptCore.dll` 生成

---

## 阶段四：C++ 侧注册 InternalCalls + ScriptComponent

**目标**：C# 调用 InternalCall 时实际执行 C++ 函数；新建 ScriptComponent 组件

**依赖**：阶段二 + 阶段三完成后才能联调

**里程碑**：给实体加 ScriptComponent(ClassName="PlayerController")，Play 后实体按 WASD 移动 ✓

- [ ] **4.1 新建 `Redflag/src/Redflag/Scripting/ScriptGlue.h`**
  - 声明：
    ```cpp
    class ScriptGlue
    {
    public:
        static void RegisterFunctions();  // 注册所有 InternalCall
    };
    ```

- [ ] **4.2 新建 `Redflag/src/Redflag/Scripting/ScriptGlue.cpp`**
  - 实现每个 InternalCall 对应的 C++ 函数：
    ```cpp
    // Transform 读取
    static void Entity_GetTranslation(uint64_t entityID, glm::vec3* outTranslation)
    {
        Scene* scene = ScriptEngine::GetSceneContext();
        Entity entity = scene->GetEntityByUUID(entityID);
        *outTranslation = entity.GetComponent<TransformComponent>().Translation;
    }

    // Transform 写入
    static void Entity_SetTranslation(uint64_t entityID, glm::vec3* translation)
    {
        Scene* scene = ScriptEngine::GetSceneContext();
        Entity entity = scene->GetEntityByUUID(entityID);
        entity.GetComponent<TransformComponent>().Translation = *translation;
    }

    // 键盘输入
    static bool Input_IsKeyDown(int keycode)
    {
        return Input::IsKeyPressed((KeyCode)keycode);
    }

    // 日志
    static void Log_Info(MonoString* message)
    {
        char* str = mono_string_to_utf8(message);
        RF_CORE_INFO("[C# Script] {}", str);
        mono_free(str);
    }
    ```
  - 在 `RegisterFunctions()` 中注册：
    ```cpp
    void ScriptGlue::RegisterFunctions()
    {
        mono_add_internal_call("Redflag.InternalCalls::Entity_GetTranslation", Entity_GetTranslation);
        mono_add_internal_call("Redflag.InternalCalls::Entity_SetTranslation", Entity_SetTranslation);
        mono_add_internal_call("Redflag.InternalCalls::Input_IsKeyDown", Input_IsKeyDown);
        mono_add_internal_call("Redflag.InternalCalls::Log_Info", Log_Info);
        mono_add_internal_call("Redflag.InternalCalls::Log_Warn", Log_Warn);
    }
    ```
  - 注册格式：`"命名空间.类名::方法名"` 必须与 C# 的 InternalCalls.cs 声明完全匹配

- [ ] **4.3 在 ScriptEngine::Init() 中调用 ScriptGlue::RegisterFunctions()**
  - 在加载 Assembly 之前注册，这样 C# 代码一运行就能找到桥梁函数

- [ ] **4.4 确保 Scene 有 UUID → Entity 映射**
  - 检查 `Scene.h` 中是否已有：
    ```cpp
    std::unordered_map<UUID, entt::entity> m_EntityMap;
    Entity GetEntityByUUID(UUID uuid);
    ```
  - 如果没有则添加；在 `CreateEntity()` / `CreateEntityWithUUID()` 中维护映射
  - 在 `DestroyEntity()` 中移除映射

- [ ] **4.5 在 `Components.h` 中添加 ScriptComponent**
  - 在现有组件列表末尾添加：
    ```cpp
    struct ScriptComponent
    {
        std::string ClassName;  // C# 类名，如 "PlayerController"
        // 实例由 ScriptEngine 管理，不存在此处
        ScriptComponent() = default;
        ScriptComponent(const ScriptComponent&) = default;
    };
    ```
  - 注意：旧的 `NativeScriptComponent` 保留不动，两套并行

- [ ] **4.6 ScriptEngine 添加场景上下文和实例管理**
  - 添加接口：
    - `static void SetSceneContext(Scene* scene)` — 设置当前活跃场景
    - `static Scene* GetSceneContext()` — InternalCall 中调用
    - `static void OnRuntimeStart(Scene* scene)` — 遍历所有 ScriptComponent，创建 C# 实例并调用 OnCreate
    - `static void OnRuntimeStop()` — 清理所有 GCHandle 和实例
    - `static void OnUpdateEntity(Entity entity, Timestep ts)` — 调用指定实体的 C# OnUpdate
    - `static bool EntityClassExists(const std::string& className)` — 检查 Assembly 中是否有该类
  - 内部存储：
    ```cpp
    static std::unordered_map<std::string, Ref<ScriptClass>> s_EntityClasses;    // 类名 → 类信息
    static std::unordered_map<UUID, Ref<ScriptInstance>> s_EntityInstances; // UUID → 活实例
    ```

- [ ] **4.7 修改 `Scene.cpp` — 集成 C# 脚本生命周期**
  - `OnRuntimeStart()` 中添加：
    ```cpp
    ScriptEngine::OnRuntimeStart(this);
    ```
  - `OnUpdateRuntime(Timestep ts)` 脚本更新部分添加（在 NativeScriptComponent 遍历之后）：
    ```cpp
    // C# 脚本更新
    {
        auto view = m_Registry.view<ScriptComponent>();
        for (auto e : view)
        {
            Entity entity = { e, this };
            ScriptEngine::OnUpdateEntity(entity, ts);
        }
    }
    ```
  - `OnRuntimeStop()` 中添加：
    ```cpp
    ScriptEngine::OnRuntimeStop();
    ```

- [ ] **4.8 修改 `SceneSerializer.cpp` — 序列化 ScriptComponent**
  - 序列化部分（`SerializeEntity` 函数中）：
    ```cpp
    if (entity.HasComponent<ScriptComponent>())
    {
        auto& sc = entity.GetComponent<ScriptComponent>();
        out << YAML::Key << "ScriptComponent";
        out << YAML::BeginMap;
        out << YAML::Key << "ClassName" << YAML::Value << sc.ClassName;
        out << YAML::EndMap;
    }
    ```
  - 反序列化部分（`DeserializeEntities` 函数中）：
    ```cpp
    auto scriptComponent = entity["ScriptComponent"];
    if (scriptComponent)
    {
        auto& sc = deserializedEntity.AddComponent<ScriptComponent>();
        sc.ClassName = scriptComponent["ClassName"].as<std::string>();
    }
    ```

- [ ] **4.9 创建测试用 C# 游戏脚本**
  - 新建 `Sandbox-ScriptProject/` 目录
  - 创建 `PlayerController.cs`：
    ```csharp
    using Redflag;

    public class PlayerController : Entity
    {
        private float speed = 5.0f;

        protected override void OnCreate()
        {
            Log.Info("PlayerController created!");
        }

        protected override void OnUpdate(float ts)
        {
            var pos = Translation;
            if (Input.IsKeyDown(KeyCode.W)) pos.Y += speed * ts;
            if (Input.IsKeyDown(KeyCode.S)) pos.Y -= speed * ts;
            if (Input.IsKeyDown(KeyCode.A)) pos.X -= speed * ts;
            if (Input.IsKeyDown(KeyCode.D)) pos.X += speed * ts;
            Translation = pos;
        }
    }
    ```
  - 引用 `Redflag-ScriptCore.dll`，用 `dotnet build` 编译出 `GameScripts.dll`

---

## 阶段五：编辑器集成

**目标**：在 RedflagForge 编辑器中给实体添加/编辑 ScriptComponent

**依赖**：阶段四

**里程碑**：编辑器 Properties 面板可添加/显示 Script 组件，Play 后脚本正常执行 ✓

- [ ] **5.1 修改 `SceneHierachyPanel.cpp` — AddComponent 菜单**
  - 在 AddComponent popup 中添加：
    ```cpp
    if (!entity.HasComponent<ScriptComponent>())
    {
        if (ImGui::MenuItem("Script"))
        {
            entity.AddComponent<ScriptComponent>();
            ImGui::CloseCurrentPopup();
        }
    }
    ```

- [ ] **5.2 修改 `SceneHierachyPanel.cpp` — DrawComponents 显示 Script UI**
  - 使用已有的 `DrawComponent` 模板函数：
    ```cpp
    DrawComponent<ScriptComponent>("Script", entity, [](auto& component)
    {
        // 类名输入框
        char buffer[256];
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, component.ClassName.c_str(), sizeof(buffer) - 1);
        if (ImGui::InputText("Class", buffer, sizeof(buffer)))
        {
            component.ClassName = buffer;
        }

        // 显示该类是否在 Assembly 中存在
        bool classExists = ScriptEngine::EntityClassExists(component.ClassName);
        if (!component.ClassName.empty() && !classExists)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.3f, 0.3f, 1));
            ImGui::Text("Class not found!");
            ImGui::PopStyleColor();
        }
    });
    ```

- [ ] **5.3 修改 `ForgeLayer.cpp` — Play/Stop 中调用 ScriptEngine**
  - 在 `OnScenePlay()` 中：
    ```cpp
    ScriptEngine::SetSceneContext(m_ActiveScene.get());
    ```
  - 在 `OnSceneStop()` 中：
    ```cpp
    ScriptEngine::SetSceneContext(nullptr);
    ```

- [ ] **5.4 修改 `Redflag.h` — 暴露新头文件**
  - 添加：`#include "Redflag/Scripting/ScriptEngine.h"`

- [ ] **5.5 在 `Scene.cpp` 的 `OnComponentAdded` 中添加 ScriptComponent 特化**
  - 模板特化（空实现即可）：
    ```cpp
    template<>
    void Scene::OnComponentAdded<ScriptComponent>(Entity entity, ScriptComponent& component) {}
    ```

- [ ] **5.6 在 Scene::DuplicateEntity 和 Scene::Copy 中处理 ScriptComponent**
  - 确保复制实体时 ScriptComponent 的 ClassName 被正确复制

---

## 文件清单汇总

### 新建文件

| 文件路径 | 用途 |
|----------|------|
| `Redflag/vendor/mono/` | Mono SDK（include + lib + bin） |
| `Redflag/src/Redflag/Scripting/ScriptEngine.h` | 脚本引擎核心：Init, LoadAssembly, ScriptClass, ScriptInstance |
| `Redflag/src/Redflag/Scripting/ScriptEngine.cpp` | 脚本引擎实现 |
| `Redflag/src/Redflag/Scripting/ScriptGlue.h` | InternalCall 注册声明 |
| `Redflag/src/Redflag/Scripting/ScriptGlue.cpp` | InternalCall C++ 实现（Transform, Input, Log） |
| `Redflag-ScriptCore/Redflag-ScriptCore.csproj` | C# 引擎基础类库项目文件 |
| `Redflag-ScriptCore/Source/Redflag/Entity.cs` | C# Entity 基类（用户脚本继承它） |
| `Redflag-ScriptCore/Source/Redflag/Vector3.cs` | C# Vector3 结构体 |
| `Redflag-ScriptCore/Source/Redflag/InternalCalls.cs` | C# InternalCall 声明 |
| `Redflag-ScriptCore/Source/Redflag/Input.cs` | C# Input 静态类 |
| `Redflag-ScriptCore/Source/Redflag/KeyCode.cs` | C# 按键枚举 |

### 修改文件

| 文件路径 | 修改内容 |
|----------|----------|
| `premake5.lua` | 添加 Mono include/lib 路径、链接库、DLL 复制 |
| `Redflag/src/Redflag/Core/Application.cpp` | ScriptEngine::Init() / Shutdown() |
| `Redflag/src/Redflag/Scene/Components.h` | 添加 ScriptComponent 结构体 |
| `Redflag/src/Redflag/Scene/Scene.h` | 添加 UUID→Entity 映射（如果缺少） |
| `Redflag/src/Redflag/Scene/Scene.cpp` | OnRuntimeStart/Stop/UpdateRuntime 集成脚本调用 |
| `Redflag/src/Redflag/Scene/SceneSerializer.cpp` | ScriptComponent 序列化/反序列化 |
| `Redflag/src/Redflag/Scene/Panel/SceneHierachyPanel.cpp` | Script 组件 UI + AddComponent 菜单项 |
| `RedflagForge/src/ForgeLayer.cpp` | Play/Stop 中设置 ScriptEngine 场景上下文 |
| `Redflag/src/Redflag.h` | 暴露 ScriptEngine.h |

---

## 决策记录

- **运行时选择 Mono**：兼容性好，TheCherno Hazel 教程同款，学习资料最多
- **不含热重载**：第一版修改脚本后需 停止 Play → 重新编译 DLL → 再 Play
- **不含字段暴露**：第一版不在编辑器中显示/编辑 C# 脚本的公共字段
- **不含 C# 自动编译**：用户需手动 `dotnet build` 编译游戏脚本
- **保留 NativeScriptComponent**：C++ 原生脚本继续可用，两套并行
- **第一版 API 范围**：只暴露 Entity/Transform/Input/Log，不含物理/碰撞回调/组件增删/场景管理

---

## 后续扩展（本次不做）

- [ ] 热重载（卸载/重新加载 App Domain）
- [ ] 编辑器暴露 C# 公共字段（通过 Mono 反射 API + ImGui 自动生成控件）
- [ ] C# 项目一键编译（编辑器内按钮触发 `dotnet build`）
- [ ] 更多 InternalCall：物理施加力、碰撞回调、组件增删、查找实体、场景切换
- [ ] C# 侧 Component 泛型访问（`GetComponent<T>()`）
- [ ] 协程（Coroutine）支持

---

## 参考资料

- [TheCherno Hazel 引擎 Scripting 系列](https://www.youtube.com/watch?v=eg7U3_LGnRc) — 主要参考
- [Hazel 源码 vendor/mono](https://github.com/TheCherno/Hazel) — Mono 预编译库来源
- [Mono 嵌入式 API 文档](https://www.mono-project.com/docs/advanced/embedding/)
