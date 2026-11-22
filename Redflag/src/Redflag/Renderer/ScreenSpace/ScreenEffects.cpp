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
        constexpr uint32_t AnimatedShadeShader[] =
#include "Shaders/AnimatedClusterShade.comp.inc"
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
        constexpr uint32_t MultiReflectionShader[] =
#include "Shaders/MultiReflection.comp.inc"
            ;
        constexpr uint32_t MaskedMultiReflectionShader[] =
#include "Shaders/MultiReflectionAlphaMask.comp.inc"
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

    }

    struct ScreenEffects::Data
    {
        ComputeDevice& Device;
        uint32_t Width, Height;
        ScreenEffectSettings Settings;
        Ref<ComputePipeline> AO, Cull, Shade, Reflections, Temporal, Diffuse;
        Ref<ComputePipeline> MaskedShade, AnimatedShade;
        Ref<ComputePipeline> SceneReflections[2][2]; // 依次按是否续追反射、是否有镂空材质选管线。
        Ref<ComputePipeline> ReflectionFilter;
        Ref<ComputeResource> Environment, DiffuseLight, RawReflections;
        Ref<ComputeResource> Frame, Output, Intermediate, Lights, Masks, History[2], Positions[2];
        uint32_t LightCapacity = 0, WordsPerCluster = 0, Previous = 0;
        uint32_t HistoryFrames = 0, StillSamples = 0, LastFrame = 0;
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
            if (scene.GetAnimatedVisibility()) {
                inputs.insert(inputs.end(), {geometry.TextureFaces, geometry.Pixels, geometry.Masks ? geometry.Masks : geometry.TextureFaces,
                    scene.GetAnimatedVisibility()});
                if (!AnimatedShade) AnimatedShade = LoadPass(Device, AnimatedShadeShader, uint32_t(inputs.size()));
                Device.Dispatch(AnimatedShade, inputs, nullptr, (Width + 7) / 8, (Height + 7) / 8);
                return;
            }
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
        // 位置取自 Vulkan 的 16 样点表。重排后，前 4 次取样已经分布在像素的不同区域。
        static const glm::ivec2 samples[] = {
            {9,9}, {4,2}, {6,14}, {11,3}, {0,8}, {13,11}, {7,5}, {15,4},
            {2,12}, {10,13}, {1,0}, {12,7}, {5,10}, {8,1}, {14,15}, {3,6}
        };
        // 每个坐标落在小格中心，16 个位置的平均值恰好是像素中心。
        glm::vec2 sample = (glm::vec2(samples[frame % 16]) + .5f) / 16.f - .5f;
        // 后 16 帧把这些点绕像素中心转 90°。镜头停下画满 32 帧时，细线的两侧共采过 32 个不同位置。
        if ((frame / 16) % 2) sample = {-sample.y, sample.x};
        glm::vec2 shift(2.f * sample.x / width, -2.f * sample.y / height);
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
        const bool sameView = stableVP == d.PreviousVP;
        // 第二帧最多用一半旧颜色，第三帧最多用三分之二。普通表面的混合比例逐步增加到用户设定的上限。
        float weight = historyValid ? std::min(settings.HistoryWeight, d.HistoryFrames / (d.HistoryFrames + 1.f)) : 0.f;
        float edgeWeight = 0.f;
        if (effect == ScreenEffect::TAA)
        {
            const bool jittered = stableProjection && projection != *stableProjection && settings.HistoryWeight > 0.f;
            const uint32_t samples = historyValid && sameView ? d.StillSamples : 0u;
            if (jittered)
            {
                // 镜头停下后重新数这个像素采了几次。细线若在 16 次里遮住 3 次，它的颜色应占 3/16。
                edgeWeight = samples / (samples + 1.f);
                if (sameView && !samples) weight = 0.f;
            }
            d.StillSamples = jittered && (!historyValid || sameView) ? std::min(samples + 1u, 1024u) : 0u;
        }
        ScreenFrame input{
            projection,
            view,
            glm::inverse(projection),
            d.PreviousVP,
            stableVP,
            glm::vec4(lighting.GetEye(), 1),
            {d.Width, d.Height, frame, static_cast<uint32_t>(lights.size())},
            {settings.AORadius, settings.AOIntensity, settings.Roughness, settings.ReflectionDistance},
            {weight, historyValid ? 1.0f : 0.0f, settings.LightRange, sameView ? 1.f : 0.f},
            {.05f, 250.f, lighting.UsesDiffuseReference() ? 1.f : 0.f, edgeWeight}};
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
                const uint32_t bounces = uint32_t(std::clamp(settings.ReflectionBounces, 1, 4));
                const bool multi = bounces > 1, masked = bool(scene.Masks);
                Ref<ComputePipeline>& pipeline = d.SceneReflections[multi][masked];
                if (scene.Masks) inputs.push_back(scene.Masks);
                if (!pipeline) {
                    struct Program { const uint32_t* Words; size_t Bytes; };
                    static constexpr Program programs[2][2]{
                        {{SceneReflectionShader,sizeof(SceneReflectionShader)}, {MaskedReflectionShader,sizeof(MaskedReflectionShader)}},
                        {{MultiReflectionShader,sizeof(MultiReflectionShader)}, {MaskedMultiReflectionShader,sizeof(MaskedMultiReflectionShader)}}};
                    const Program& shader=programs[multi][masked];
                    pipeline=d.Device.CreatePipeline(shader.Words,shader.Bytes,std::vector<ComputeBinding>(inputs.size(),ComputeBinding::Buffer),
                        (multi ? 6 : 5) * sizeof(uint32_t));
                }
                const uint32_t options[] = {material, traceScreen, specularSky,
                    settings.ReflectProbeLight && scene.Probes, settings.DiffuseEnvironment && d.Environment, bounces};
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
