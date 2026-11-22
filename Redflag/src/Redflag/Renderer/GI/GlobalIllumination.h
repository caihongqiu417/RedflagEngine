#pragma once

#include "Redflag/Renderer/RHI/ComputeDevice.h"
#include "FogSettings.h"
#include <filesystem>
#include <map>
#include <optional>
#include <glm/glm.hpp>

namespace Redflag
{

    // 每个像素记录最近的表面。Position.w 为 0 时是背景，有模型时从 Normal.w 和 Albedo.w 读粗糙度和金属度。
    struct GISurface
    {
        glm::vec4 Position, Normal, Albedo;
    };

    struct GILight
    {
        glm::vec4 Position, Intensity; // Intensity.rgb 是各色光的强度，w 是场景中的照明半径。w 为零时只随距离减弱。

        /// <param name="directionOfTravel">光线在世界中传播的非零方向。例如正午向下照时填 (0,-1,0)。</param>
        static GILight Directional(glm::vec3 directionOfTravel, glm::vec3 intensity, bool castShadow)
        {
            // 着色时用从表面指向灯的方向。第四个数区分开启和关闭阴影的方向光。
            return { glm::vec4(-glm::normalize(directionOfTravel), castShadow ? -1.f : -2.f),
                     glm::vec4(intensity, 0.f) };
        }
    };

    struct GIEmissionOverride
    {
        bool DoubleSided = false;
        // 未修改贴图时继续读取模型里的图片。空路径表示从这个部件移除了贴图。
        std::optional<std::filesystem::path> Image;

        bool operator==(const GIEmissionOverride& other) const
        {
            return DoubleSided == other.DoubleSided && Image == other.Image;
        }
    };

    struct GIMaterialMaps
    {
        // 未指定时沿用模型里的贴图。空路径表示移除，非空路径指向替换用的图片。
        std::optional<std::filesystem::path> BaseColor, MetallicRoughness;

        bool operator==(const GIMaterialMaps& other) const
        {
            return BaseColor == other.BaseColor && MetallicRoughness == other.MetallicRoughness;
        }
    };

    struct GIMaskSettings
    {
        float Alpha = 1.f;
        float Cutoff = .5f; // 负值表示这个部件已改回不裁剪的材质。
        std::optional<std::filesystem::path> Opacity; // 未指定时沿用模型的透明贴图，空路径移除它。

        bool operator==(const GIMaskSettings& other) const
        {
            return Alpha == other.Alpha && Cutoff == other.Cutoff && Opacity == other.Opacity;
        }
    };

    struct GIModel
    {
        std::filesystem::path Path; // 空路径表示引擎的单位立方体。
        glm::mat4 Transform{1};
        glm::vec3 Tint{1};
        // 按 Model 子网格顺序排列。w 为 1 时采用该实体指定的底色。
        std::vector<glm::vec4> MaterialColors;
        std::vector<uint8_t> TransparentParts;
        uint64_t EntityID = 0; // 按实体编号排列模型。另存为后也按同一顺序建树，原来的探针文件仍可使用。
        std::vector<glm::vec2> MaterialFactors; // 各子网格实际使用的粗糙度、金属度。
        std::vector<glm::vec3> MaterialEmissions; // 各部件发出的光，用线性 RGB 表示。数组为空时读取模型自带的颜色和强度。
        std::map<size_t, GIEmissionOverride> EmissionOverrides;
        std::map<size_t, GIMaterialMaps> MaterialMaps;
        std::map<size_t, GIMaskSettings> MaterialMasks;
    };

    enum class GIMethod
    {
        Direct,
        DDGI,
        ReSTIRDI,
        ReSTIRGI,
        Indirect,
        DirectReference,
        IndirectReference
    };

    struct GIReflectionResources
    {
        Ref<ComputeResource> Nodes, Triangles, Lights, SceneInfo;
        Ref<ComputeResource> TextureFaces, Pixels, Emitters, Probes;
        Ref<ComputeResource> Masks;
    };

    /* 计算点光源和方向光直接照到物体的光，以及场景反射到表面的光。DDGI 探针和 ReSTIR GI 也能收到发光材质的光。 */
    class GlobalIllumination
    {
      public:
        // 对照测试把模型最长的一边缩放到 20 个场景单位，固定点光源的位置，按漫反射计算直接光。
        GlobalIllumination(ComputeDevice &device, const std::filesystem::path &modelPath, uint32_t width,
                           uint32_t height);
        // 编辑器和游戏传入场景原有的变换与材质，再用 SetLights 指定照亮场景的灯。
        GlobalIllumination(ComputeDevice &device, const std::vector<GIModel> &models, uint32_t width, uint32_t height);
        ~GlobalIllumination();

        // 拖大场景面板时，颜色和表面位置图跟着变大。模型、贴图和房间里的探针继续使用。
        void Resize(uint32_t width, uint32_t height);
        void SetCamera(const glm::mat4 &viewProjection, glm::vec3 eye, bool resetHistory = true);
        // 场景随后会重画相机看到的表面。镜头变化且 resetHistory 为 false 时，ReSTIR 会比较同一像素前后两帧的位置和法线，再决定能否沿用上次选的灯或反射点。
        void SetRasterCamera(const glm::mat4& viewProjection, glm::vec3 eye, bool resetHistory);
        void SetLights(const std::vector<GILight> &lights);
        /// <summary>探针射线穿过窗口时读取这份环境图。空引用表示场景没有天光。</summary>
        /// <param name="contentKey">加载环境时由 EnvironmentLighting::Load 算出的像素校验值。</param>
        bool SetProbeEnvironment(const Ref<ComputeResource>& lighting, uint64_t contentKey);
        void ResetHistory();
        void Render(GIMethod method, uint32_t samples = 1);

        /// <summary>开启后，探针也接收上一轮的墙面反光。切换会清掉内存中的烘焙结果，原文件仍然保留。</summary>
        /// <returns>模式实际改变时返回 true，调用者可按新设置重新查找烘焙文件。</returns>
        bool SetProbeFeedback(bool enabled);

        /// <summary>算好房间里各个探针收到的反射光并保存。移动相机后仍可读取这份文件。</summary>
        void BakeProbes(const std::filesystem::path& file);
        bool LoadProbes(const std::filesystem::path& file);
        void SaveProbes(const std::filesystem::path& file) const;
        bool HasBakedProbes() const;
        // useReSTIR 为 true 时为每个像素选一盏灯，否则逐盏计算。直接光单独保存，编辑器稍后把它与间接光相加。
        void RenderDirect(bool useReSTIR);
        /// <summary>沿视线逐段计算雾散射进镜头的光，再加上穿过雾的场景颜色。遇到物体或达到设定的距离就停止。</summary>
        /// <param name="color">色调映射前的场景颜色。每个像素占四个 float，前三个是线性 RGB。</param>
        Ref<ComputeResource> RenderFog(const Ref<ComputeResource>& color, const FogSettings& fog);
        const Ref<ComputeResource> &GetDirectOutput() const;

        const Ref<ComputeResource> &GetSurfaces() const;
        const Ref<ComputeResource> &GetOutput() const;
        const glm::mat4 &GetViewProjection() const;
        glm::vec3 GetEye() const;
        const std::vector<GILight> &GetLights() const;
        bool UsesDiffuseReference() const;

        // 分簇着色与逐灯着色共用场景的遮挡数据。包围盒和三角形各保存在一份显卡缓冲区中。
        const Ref<ComputeResource>& GetSceneNodes() const;
        const Ref<ComputeResource>& GetSceneTriangles() const;
        // 反射光线和间接光使用同一份场景几何与材质。探针还没有算过时，Probes 为空。
        GIReflectionResources GetReflectionResources() const;

        uint32_t GetFrameIndex() const;
        void SetSeed(uint32_t seed);

      private:
        bool UpdateCamera(const glm::mat4& viewProjection, glm::vec3 eye, bool resetHistory);
        struct Data;
        Scope<Data> m_Data;
    };
}
