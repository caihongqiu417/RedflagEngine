#include "rfpch.h"
#include "ScreenEffects.h"
#include "Redflag/Renderer/GI/GlobalIllumination.h"

namespace Redflag
{
    namespace
    {
        constexpr uint32_t AOShader[] =
#include "Shaders/GTAO.comp.inc"
            ;
        constexpr uint32_t CullShader[] =
#include "Shaders/ClusterCull.comp.inc"
            ;
        constexpr uint32_t ShadeShader[] =
#include "Shaders/ClusterShade.comp.inc"
            ;
        constexpr uint32_t MaskedShadeShader[] =
#include "Shaders/ClusterShadeAlphaMask.comp.inc"
            ;
        constexpr uint32_t ReflectionShader[] =
#include "Shaders/StochasticSSR.comp.inc"
            ;
        constexpr uint32_t SceneReflectionShader[] =
#include "Shaders/StochasticSceneSSR.comp.inc"
            ;
        constexpr uint32_t MaskedReflectionShader[] =
#include "Shaders/StochasticSceneSSRAlphaMask.comp.inc"
            ;
        constexpr uint32_t ReflectionFilterShader[] =
#include "Shaders/ReflectionFilter.comp.inc"
            ;
        constexpr uint32_t TemporalShader[] =
#include "Shaders/TAA.comp.inc"
            ;
        constexpr uint32_t DiffuseShader[] =
#include "Shaders/EnvironmentDiffuse.comp.inc"
            ;

        struct ScreenFrame
        {
            glm::mat4 Projection, View, InverseProjection, PreviousVP, StableVP;
            glm::vec4 Eye;
            glm::uvec4 Extent;
            glm::vec4 Material, Temporal, DepthRange;
        };
        static_assert(sizeof(ScreenFrame) == 400);

        template <size_t N>
        Ref<ComputePipeline> LoadPass(ComputeDevice& device, const uint32_t (&code)[N], uint32_t buffers)
        {
            return device.CreatePipeline(code, sizeof(code),
                                         std::vector<ComputeBinding>(buffers, ComputeBinding::Buffer));
        }

        float Halton(uint32_t index, uint32_t base)
        {
            float sum = 0.0f, place = 1.0f;
            while (index)
            {
                place /= base;
                sum += place * (index % base);
                index /= base;
            }
            return sum;
        }
    }

    struct ScreenEffects::Data
    {
        ComputeDevice& Device;
        uint32_t Width, Height;
        ScreenEffectSettings Settings;
        Ref<ComputePipeline> AO, Cull, Shade, Reflections, SceneReflections, Temporal, Diffuse;
        Ref<ComputePipeline> MaskedShade, MaskedSceneReflections;
        Ref<ComputePipeline> ReflectionFilter;
        Ref<ComputeResource> Environment, DiffuseLight, RawReflections;
        Ref<ComputeResource> Frame, Output, Intermediate, Lights, Masks, History[2], Positions[2];
        uint32_t LightCapacity = 0, WordsPerCluster = 0, Previous = 0;
        uint32_t HistoryFrames = 0, LastFrame = 0;
        glm::mat4 PreviousVP{1.0f};

        Data(ComputeDevice& device, uint32_t width, uint32_t height) : Device(device), Width(width), Height(height)
        {
            AO = LoadPass(device, AOShader, 4);
            Cull = LoadPass(device, CullShader, 4);
            Shade = LoadPass(device, ShadeShader, 7);
            Reflections = device.CreatePipeline(ReflectionShader, sizeof(ReflectionShader),
                                                std::vector<ComputeBinding>(5, ComputeBinding::Buffer), 3 * sizeof(uint32_t));
            Temporal = LoadPass(device, TemporalShader, 7);

            Frame = device.CreateBuffer(sizeof(ScreenFrame));
            size_t bytes = size_t(width) * height * sizeof(glm::vec4);
            Output = device.CreateBuffer(bytes);
            for (int i = 0; i < 2; ++i)
            {
                History[i] = device.CreateBuffer(bytes);
                Positions[i] = device.CreateBuffer(bytes);
            }
        }

        void DrawLights(const GlobalIllumination& scene)
        {
            const auto& lights = scene.GetLights();
            const auto& surfaces = scene.GetSurfaces();
            uint32_t count = std::max(1u, static_cast<uint32_t>(lights.size()));
            // Upload 要求上传字节数与缓冲区大小一致。例如三盏灯删到两盏，就换成只装两盏的灯光数组。
            if (count != LightCapacity)
            {
                Lights = Device.CreateBuffer(count * sizeof(GILight));
                LightCapacity = count;
            }
            if (!lights.empty())
                Device.Upload(Lights, lights.data(), lights.size() * sizeof(GILight));

            uint32_t words = (count + 31) / 32;
            uint32_t entries = ((Width + 15) / 16) * ((Height + 15) / 16) * 16 * words;
            if (words != WordsPerCluster)
            {
                Masks = Device.CreateBuffer(size_t(entries) * sizeof(uint32_t));
                WordsPerCluster = words;
            }
            Device.Dispatch(Cull, {Frame, surfaces, Lights, Masks}, nullptr, (entries + 63) / 64);
            GIReflectionResources geometry = scene.GetReflectionResources();
            std::vector<Ref<ComputeResource>> inputs{Frame, surfaces, Lights, Masks, Output, geometry.Nodes, geometry.Triangles};
            if (geometry.Masks)
            {
                inputs.insert(inputs.end(), {geometry.TextureFaces, geometry.Pixels, geometry.Masks});
                if (!MaskedShade) MaskedShade = LoadPass(Device, MaskedShadeShader, uint32_t(inputs.size()));
            }
            Device.Dispatch(geometry.Masks ? MaskedShade : Shade, inputs, nullptr, (Width + 7) / 8, (Height + 7) / 8);
        }
    };

    ScreenEffects::ScreenEffects(ComputeDevice& device, uint32_t width, uint32_t height)
        : m_Data(CreateScope<Data>(device, width, height))
    {
    }
    ScreenEffects::~ScreenEffects() = default;

    ScreenEffectSettings& ScreenEffects::GetSettings()
    {
        return m_Data->Settings;
    }

    void ScreenEffects::SetEnvironment(const Ref<ComputeResource>& lighting)
    {
        m_Data->Environment = lighting;
    }
    void ScreenEffects::ResetHistory()
    {
        m_Data->HistoryFrames = 0;
    }

    glm::mat4 ScreenEffects::JitterProjection(const glm::mat4& projection, uint32_t frame, uint32_t width,
                                              uint32_t height)
    {
        // 采样点按 16 帧循环，每帧在一个像素内取不同的位置，用来平滑物体边缘。
        uint32_t index = frame % 16 + 1;
        glm::vec2 shift(2.0f * (Halton(index, 2) - .5f) / width, -2.0f * (Halton(index, 3) - .5f) / height);
        glm::mat4 result = projection;
        for (int col = 0; col < 4; ++col)
        {
            result[col][0] += shift.x * projection[col][3];
            result[col][1] += shift.y * projection[col][3];
        }
        return result;
    }

    Ref<ComputeResource> ScreenEffects::Render(ScreenEffect effect, const GlobalIllumination& lighting,
                                               const glm::mat4& projection, const glm::mat4& view, uint32_t frame,
                                               const Ref<ComputeResource>& inputColor,
                                               const glm::mat4* stableProjection)
    {
        auto& d = *m_Data;
        const auto& settings = d.Settings;
        const auto& lights = lighting.GetLights();
        const auto& surfaces = lighting.GetSurfaces();
        const auto& color = inputColor ? inputColor : lighting.GetOutput();

        const glm::mat4 stableVP = (stableProjection ? *stableProjection : projection) * view;
        const bool historyValid = d.HistoryFrames > 0 && frame == d.LastFrame + 1;
        // 默认设置下，第二帧混入一半上一帧的颜色，第三帧混入三分之二。HistoryWeight 限制这个比例，防止新画面的颜色越占越少。
        float weight = historyValid ? std::min(settings.HistoryWeight, d.HistoryFrames / (d.HistoryFrames + 1.f)) : 0.f;
        ScreenFrame input{
            projection,
            view,
            glm::inverse(projection),
            d.PreviousVP,
            stableVP,
            glm::vec4(lighting.GetEye(), 1),
            {d.Width, d.Height, frame, static_cast<uint32_t>(lights.size())},
            {settings.AORadius, settings.AOIntensity, settings.Roughness, settings.ReflectionDistance},
            {weight, historyValid ? 1.0f : 0.0f, settings.LightRange, stableVP == d.PreviousVP ? 1.f : 0.f},
            {.05f, 250.f, lighting.UsesDiffuseReference() ? 1.f : 0.f, 0}};
        d.Device.Upload(d.Frame, &input, sizeof(input));

        uint32_t gx = (d.Width + 7) / 8, gy = (d.Height + 7) / 8;
        if (effect == ScreenEffect::TAA)
        {
            uint32_t next = 1 - d.Previous;
            d.Device.Dispatch(d.Temporal,
                              {d.Frame, surfaces, color, d.History[d.Previous], d.Positions[d.Previous],
                               d.History[next], d.Positions[next]},
                              nullptr, gx, gy);
            d.Previous = next;
            d.PreviousVP = stableVP;
            d.HistoryFrames = historyValid ? std::min(1024u, d.HistoryFrames + 1u) : 1u;
            d.LastFrame = frame;
            return d.History[next];
        }

        if (effect == ScreenEffect::ClusteredLighting)
        {
            d.DrawLights(lighting);
            return d.Output;
        }
        if (effect == ScreenEffect::EnvironmentDiffuse)
        {
            if (!d.Environment) throw std::logic_error("Diffuse IBL requires a scene environment");
            if (!d.Diffuse)
            {
                d.Diffuse = LoadPass(d.Device, DiffuseShader, 4);
                // 分簇直接光和 GTAO 还会用到 Output，这份漫反射要一直保留到三者合成。
                d.DiffuseLight = d.Device.CreateBuffer(size_t(d.Width) * d.Height * sizeof(glm::vec4));
            }
            d.Device.Dispatch(d.Diffuse, {d.Frame, surfaces, d.Environment, d.DiffuseLight}, nullptr, gx, gy);
            return d.DiffuseLight;
        }
        // 上一道效果的结果还在使用。反射会读邻居的颜色，必须另找一块地方写。
        if (color == d.Output && !d.Intermediate)
            d.Intermediate = d.Device.CreateBuffer(size_t(d.Width) * d.Height * sizeof(glm::vec4));
        const auto& destination = color == d.Output ? d.Intermediate : d.Output;
        if (effect == ScreenEffect::GTAO)
            d.Device.Dispatch(d.AO, {d.Frame, surfaces, color, destination}, nullptr, gx, gy);
        else
        {
            const bool material = effect == ScreenEffect::MaterialReflections;
            const bool traceScreen = !material || settings.TraceReflections;
            const bool specularSky = material && settings.SpecularEnvironment && d.Environment;
            const bool filter = traceScreen && settings.DenoiseReflections;
            if (filter && !d.ReflectionFilter)
            {
                d.ReflectionFilter = d.Device.CreatePipeline(ReflectionFilterShader, sizeof(ReflectionFilterShader),
                    std::vector<ComputeBinding>(5, ComputeBinding::Buffer), sizeof(uint32_t));
                d.RawReflections = d.Device.CreateBuffer(size_t(d.Width) * d.Height * sizeof(glm::vec4));
            }
            const auto& traced = filter ? d.RawReflections : destination;
            // 没有环境图时借用 Frame 占位，着色器会跳过环境光计算。
            const auto& sky = d.Environment ? d.Environment : d.Frame;
            if (settings.TraceScene && traceScreen)
            {
                GIReflectionResources scene = lighting.GetReflectionResources();
                std::vector<Ref<ComputeResource>> inputs = {d.Frame, surfaces, color, traced, sky,
                    scene.Nodes, scene.Triangles, scene.Lights, scene.SceneInfo, scene.TextureFaces,
                    scene.Pixels, scene.Emitters, scene.Probes ? scene.Probes : d.Frame};
                Ref<ComputePipeline>& pipeline = scene.Masks ? d.MaskedSceneReflections : d.SceneReflections;
                if (scene.Masks) inputs.push_back(scene.Masks);
                if (!pipeline)
                    pipeline = d.Device.CreatePipeline(scene.Masks ? MaskedReflectionShader : SceneReflectionShader,
                        scene.Masks ? sizeof(MaskedReflectionShader) : sizeof(SceneReflectionShader),
                        std::vector<ComputeBinding>(inputs.size(), ComputeBinding::Buffer), 5 * sizeof(uint32_t));
                const uint32_t options[] = {material, traceScreen, specularSky,
                    settings.ReflectProbeLight && scene.Probes, settings.DiffuseEnvironment && d.Environment};
                d.Device.Dispatch(pipeline, inputs, options, gx, gy);
            }
            else
            {
                const uint32_t options[] = {material, traceScreen, specularSky};
                d.Device.Dispatch(d.Reflections, {d.Frame, surfaces, color, traced, sky}, options, gx, gy);
            }
            if (filter)
            {
                const uint32_t useMaterial = material;
                d.Device.Dispatch(d.ReflectionFilter, {d.Frame, surfaces, color, traced, destination}, &useMaterial, gx, gy);
            }
        }
        return destination;
    }
}
