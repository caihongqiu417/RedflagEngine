#include "rfpch.h"
#include "GlobalIllumination.h"
#include "GIScene.h"
#include "ProbeBake.h"
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cstring>

namespace Redflag
{

    struct GlobalIllumination::Data
    {
        struct SceneInfo
        {
            glm::mat4 InverseVP;
            glm::vec4 Eye, GridOrigin, GridStep;
            glm::uvec4 ImageSize, GridSize;
        } Info{};
        struct Work
        {
            uint32_t Stage, Frame, Samples, Base, Seed;
        };

        ComputeDevice &Device;
        Ref<ComputePipeline> Pipeline;
        Ref<ComputePipeline> FogPipeline;
        Ref<ComputeResource> FogColor;
        std::vector<Ref<ComputeResource>> Bindings;
        std::vector<GILight> Lights;
        glm::mat4 ViewProjection{0};
        uint32_t Frame = 0, Seed = 137;
        GIMethod Method = GIMethod::Direct;
        Ref<ComputeResource> DirectOutput, DirectHistory[2];
        Ref<ComputeResource> Empty;
        Ref<ComputeResource> Emitters, Masks;
        uint32_t DirectFrame = 0;
        uint32_t ProbeFrame = 0;
        bool ProbeFeedback = true;
        // 烘焙时探针的每个方向取 64 个样本。开启多次反射时算 4 轮，后一轮会读前一轮的光。
        // 这是项目的烘焙设置。增加样本会多查几条光线，增加轮数会多算几次反射。
        static constexpr uint32_t BakeRays = 64, BakeRounds = 4;
        uint64_t GeometryKey = ProbeBake::HashStart;
        std::vector<glm::vec4> BakedProbes;
        std::filesystem::path UnreadableTexture;

        Data(ComputeDevice &device, const std::vector<GIModel> &models, uint32_t width, uint32_t height, bool normalize)
            : Device(device), Bindings(14)
        {
            auto ordered = models;
            std::stable_sort(ordered.begin(), ordered.end(), [](const GIModel& a, const GIModel& b) {
                return a.EntityID < b.EntityID;
            });
            auto scene = LoadGIScene(ordered, normalize);
            UnreadableTexture = std::move(scene.UnreadableTexture);
            const glm::uvec3 grid{12, 6, 7};
            const glm::vec3 step = glm::max(scene.Max - scene.Min, glm::vec3(.01f)) / glm::vec3(grid);
            Info.ImageSize = {width, height, 0, scene.Nodes.size()};
            Info.GridSize = glm::uvec4(grid, 0);
            // w 为 1 时，测试场景只算漫反射。这样换一种选灯方法，仍能用同一面墙的亮度比较结果。
            Info.GridOrigin = glm::vec4(scene.Min + .5f * step, normalize ? 1.f : 0.f);
            Info.GridStep = glm::vec4(step, 0);
            Bindings[0] = device.CreateBuffer(scene.Nodes.size() * sizeof(GINode), scene.Nodes.data());
            Bindings[1] = device.CreateBuffer(scene.Triangles.size() * sizeof(GITriangle), scene.Triangles.data());
            Bindings[3] = device.CreateBuffer(sizeof(GILight));
            Bindings[4] = device.CreateBuffer(size_t(grid.x) * grid.y * grid.z * 6 * 2 * sizeof(glm::vec4));
            Bindings[13] = device.CreateBuffer(size_t(grid.x) * grid.y * grid.z * 6 * 2 * sizeof(glm::vec4));
            Bindings[9] = device.CreateBuffer(sizeof(Info), &Info);
            Bindings[11] = device.CreateBuffer(std::max(size_t(1), scene.TextureFaces.size()) * sizeof(GITextureFace),
                                               scene.TextureFaces.data());
            Bindings[12] =
                device.CreateBuffer(std::max(size_t(1), scene.Pixels.size()) * sizeof(uint32_t), scene.Pixels.data());
            static const uint32_t reflected[] =
#include "Shaders/GI.comp.inc"
                ;
            static const uint32_t withEmission[] =
#include "Shaders/GIEmission.comp.inc"
                ;
            static const uint32_t masked[] =
#include "Shaders/GIAlphaMask.comp.inc"
                ;
            static const uint32_t maskedEmission[] =
#include "Shaders/GIEmissionAlphaMask.comp.inc"
                ;
            const bool emits = !scene.Emissions.empty();
            const uint32_t* code = emits ? withEmission : reflected;
            size_t codeBytes = emits ? sizeof(withEmission) : sizeof(reflected);
            if (emits)
            {
                Emitters = device.CreateBuffer(scene.Emissions.size() * sizeof(GIEmission), scene.Emissions.data());
                Bindings.push_back(Emitters);
            }
            if (!scene.Masks.empty())
            {
                code = emits ? maskedEmission : masked;
                codeBytes = emits ? sizeof(maskedEmission) : sizeof(masked);
                Masks = device.CreateBuffer(scene.Masks.size() * sizeof(GIMask), scene.Masks.data());
                Bindings.push_back(Masks);
            }
            // 把模型顶点和贴图像素等数据算进烘焙文件的校验值。即使文件名没变，换过模型或贴图也会重新算光。
            auto hashArray = [&](const auto& values) {
                GeometryKey = ProbeBake::Hash(GeometryKey, values.data(), values.size() * sizeof(values[0]));
            };
            GeometryKey = ProbeBake::Hash(GeometryKey, code, codeBytes);
            GeometryKey = ProbeBake::Hash(GeometryKey, &Info.GridSize, sizeof(Info.GridSize));
            hashArray(scene.Triangles);
            hashArray(scene.TextureFaces);
            hashArray(scene.Pixels);
            hashArray(scene.Emissions);
            hashArray(scene.Masks);
            Pipeline = device.CreatePipeline(
                code, codeBytes, std::vector<ComputeBinding>(Bindings.size(), ComputeBinding::Buffer), sizeof(Work));
            Empty = Device.CreateBuffer(sizeof(glm::vec4));
            CreateViewport(width, height);
        }

        void CreateViewport(uint32_t width, uint32_t height)
        {
            if (!width || !height)
                throw std::invalid_argument("GI requires a non-empty viewport");
            Info.ImageSize.x = width;
            Info.ImageSize.y = height;
            Info.GridSize.w = 0;
            const size_t pixels = size_t(width) * height;
            Bindings[2] = Device.CreateBuffer(pixels * sizeof(GISurface));
            Bindings[8] = Device.CreateBuffer(pixels * sizeof(glm::vec4));

            // ReSTIR 开始选灯或反射点时，才为每个像素分配记录。相机移动且需要沿用这些结果时，再保存上一帧的表面位置和法线。
            // 在此之前，各个数组都用同一小块显存占位。
            Bindings[5] = Bindings[6] = Bindings[7] = Bindings[10] = Empty;
            DirectOutput.reset();
            FogColor.reset();
            DirectHistory[0].reset();
            DirectHistory[1].reset();
            Frame = DirectFrame = 0;
        }

        void PrepareReservoirs(bool useMainHistory)
        {
            size_t bytes = size_t(Info.ImageSize.x) * Info.ImageSize.y * 4 * sizeof(glm::vec4);
            if (Bindings[6] == Empty)
                Bindings[6] = Device.CreateBuffer(bytes);
            if (useMainHistory && Bindings[5] == Empty)
            {
                Bindings[5] = Device.CreateBuffer(bytes);
                Bindings[7] = Device.CreateBuffer(bytes);
            }
        }

        void Dispatch(uint32_t stage, uint32_t samples = 1, uint32_t base = 0)
        {
            Work work{stage, Frame, samples, base, Seed};
            uint32_t count = Info.ImageSize.x * Info.ImageSize.y;
            Device.Dispatch(Pipeline, Bindings, &work, (count + 63) / 64);
        }

        uint32_t ProbeFaces() const { return Info.GridSize.x * Info.GridSize.y * Info.GridSize.z * 6; }

        void UpdateProbes(std::vector<Ref<ComputeResource>>& buffers, uint32_t round, uint32_t samples, bool smooth)
        {
            const uint32_t flags = (ProbeFeedback ? 1u : 0u) | (smooth ? 2u : 0u);
            Work work{9, round, samples, flags, Seed};
            Device.Dispatch(Pipeline, buffers, &work, (ProbeFaces() + 63) / 64);
            // 所有探针都算完这轮，再让下一轮和画面读取它。邻居不会读到一半新、一半旧的光。
            std::swap(buffers[4], buffers[13]);
        }

        std::vector<GILight> BakeLights() const
        {
            // 固定种子选中第 i 盏灯时，两次烘焙应读到同一盏灯。先按 RGB 强度排列，再用半径、位置或方向区分同色的灯。
            auto fields = [](const GILight& light) {
                const glm::uvec4 color = glm::floatBitsToUint(light.Intensity);
                const glm::uvec4 position = glm::floatBitsToUint(light.Position);
                return std::array<uint32_t, 8>{color.r, color.g, color.b, color.w, position.x, position.y, position.z, position.w};
            };
            std::vector<GILight> lights = Lights;
            std::sort(lights.begin(), lights.end(), [&](const GILight& a, const GILight& b) { return fields(a) < fields(b); });
            return lights;
        }

        uint64_t ProbeKey(const std::vector<GILight>& bakeLights) const
        {
            auto key = ProbeBake::Hash(GeometryKey, &Seed, sizeof(Seed));
            const uint32_t bake[] = { BakeRays, ProbeFeedback ? BakeRounds : 1u };
            key = ProbeBake::Hash(key, bake, sizeof(bake));
            if (bakeLights.size() > 1) {
                // 旧版本按实体出现的顺序抽灯。两盏以上要重烘，只有一盏灯的场景可继续读原文件。
                constexpr uint32_t lightOrderVersion = 2;
                key = ProbeBake::Hash(key, &lightOrderVersion, sizeof(lightOrderVersion));
            }
            for (const GILight& light : bakeLights) {
                const uint64_t lightKey = ProbeBake::Hash(ProbeBake::HashStart, &light, sizeof(light));
                key = ProbeBake::Hash(key, &lightKey, sizeof(lightKey));
            }
            return key;
        }
    };

    GlobalIllumination::GlobalIllumination(ComputeDevice &device, const std::filesystem::path &path, uint32_t width,
                                           uint32_t height)
        : m_Data(CreateScope<Data>(device, std::vector<GIModel>{{path}}, width, height, true))
    {
        // 参考场景使用固定灯阵，方便比较不同方法在同一处墙角估算出的亮度。
        std::vector<GILight> lights;
        for (int x = 0; x < 8; ++x)
            for (int z = 0; z < 2; ++z)
            {
                const glm::vec3 color = z ? glm::vec3(8, 5, 3) : glm::vec3(3, 5, 8);
                lights.push_back({{float(x) * 2.3f - 8, -1.8f, z ? .55f : -.55f, 1}, {color, 0}});
            }
        SetLights(lights);
        const glm::vec3 eye(-5.5f, -2.2f, 0);
        auto view = glm::lookAt(eye, glm::vec3(6, -1.8, 0), glm::vec3(0, 1, 0));
        SetCamera(glm::perspective(glm::radians(65.f), float(width) / height, .05f, 100.f) * view, eye);
    }

    GlobalIllumination::GlobalIllumination(ComputeDevice &device, const std::vector<GIModel> &models, uint32_t width,
                                           uint32_t height)
        : m_Data(CreateScope<Data>(device, models, width, height, false))
    {
        const glm::vec3 eye(0, 0, 10);
        SetCamera(glm::perspective(glm::radians(65.f), float(width) / height, .05f, 100.f) *
                      glm::lookAt(eye, glm::vec3(0), glm::vec3(0, 1, 0)),
                  eye);
    }

    GlobalIllumination::~GlobalIllumination() = default;

    void GlobalIllumination::Resize(uint32_t width, uint32_t height)
    {
        auto &d = *m_Data;
        if (width == d.Info.ImageSize.x && height == d.Info.ImageSize.y)
            return;
        d.CreateViewport(width, height);
        d.Device.Upload(d.Bindings[9], &d.Info, sizeof(d.Info));
        d.Dispatch(0);
    }

    void GlobalIllumination::SetCamera(const glm::mat4 &vp, glm::vec3 eye, bool resetHistory)
    {
        if (UpdateCamera(vp, eye, resetHistory)) m_Data->Dispatch(0);
    }

    void GlobalIllumination::SetRasterCamera(const glm::mat4 &vp, glm::vec3 eye, bool resetHistory)
    {
        UpdateCamera(vp, eye, resetHistory);
    }

    bool GlobalIllumination::UpdateCamera(const glm::mat4 &vp, glm::vec3 eye, bool resetHistory)
    {
        auto &d = *m_Data;
        if (d.ViewProjection == vp && glm::vec3(d.Info.Eye) == eye)
        {
            if (d.Info.GridSize.w)
            {
                d.Info.GridSize.w = 0;
                d.Device.Upload(d.Bindings[9], &d.Info, sizeof(d.Info));
            }
            return false;
        }
        if (!resetHistory)
        {
            if (d.Bindings[10] == d.Empty)
                d.Bindings[10] =
                    d.Device.CreateBuffer(size_t(d.Info.ImageSize.x) * d.Info.ImageSize.y * sizeof(GISurface));
            d.Dispatch(11);
        }
        d.Info.GridSize.w = resetHistory ? 0 : 1;
        d.ViewProjection = vp;
        d.Info.InverseVP = glm::inverse(vp);
        d.Info.Eye = glm::vec4(eye, 1);
        d.Device.Upload(d.Bindings[9], &d.Info, sizeof(d.Info));
        if (resetHistory)
            ResetHistory();
        return true;
    }

    void GlobalIllumination::SetLights(const std::vector<GILight> &lights)
    {
        auto &d = *m_Data;
        if (lights.size() == d.Lights.size() &&
            (lights.empty() || std::memcmp(lights.data(), d.Lights.data(), lights.size() * sizeof(GILight)) == 0))
            return;
        d.Lights = lights;
        d.BakedProbes.clear();
        d.ProbeFrame = 0;
        d.Info.ImageSize.z = static_cast<uint32_t>(lights.size());
        d.Bindings[3] = d.Device.CreateBuffer(std::max<size_t>(1, lights.size()) * sizeof(GILight),
                                              lights.empty() ? nullptr : lights.data());
        d.Device.Upload(d.Bindings[9], &d.Info, sizeof(d.Info));
        ResetHistory();
    }

    void GlobalIllumination::ResetHistory()
    {
        m_Data->Frame = 0;
        m_Data->DirectFrame = 0;
    }
    void GlobalIllumination::SetSeed(uint32_t seed)
    {
        if (m_Data->Seed != seed)
            m_Data->BakedProbes.clear();
        m_Data->ProbeFrame = 0;
        m_Data->Seed = seed;
        ResetHistory();
    }
    uint32_t GlobalIllumination::GetFrameIndex() const
    {
        return m_Data->Frame;
    }
    const Ref<ComputeResource> &GlobalIllumination::GetSurfaces() const
    {
        return m_Data->Bindings[2];
    }
    const Ref<ComputeResource> &GlobalIllumination::GetOutput() const
    {
        return m_Data->Bindings[8];
    }
    const Ref<ComputeResource> &GlobalIllumination::GetDirectOutput() const
    {
        return m_Data->DirectOutput;
    }

    Ref<ComputeResource> GlobalIllumination::RenderFog(const Ref<ComputeResource>& color, const FogSettings& fog)
    {
        if (fog.Steps <= 0 || fog.Density < 0 || fog.Distance < 0 || fog.Albedo < 0 || fog.Albedo > 1 ||
            std::abs(fog.Anisotropy) >= 1)
            throw std::invalid_argument("Invalid volumetric fog settings");
        if (fog.Density == 0 || fog.Distance == 0) return color;

        auto& d = *m_Data;
        const size_t pixels = size_t(d.Info.ImageSize.x) * d.Info.ImageSize.y;
        if (!d.FogColor) d.FogColor = d.Device.CreateBuffer(pixels * sizeof(glm::vec4));
        std::vector<Ref<ComputeResource>> inputs = {d.Bindings[0], d.Bindings[1], d.Bindings[2],
            d.Bindings[3], d.Bindings[9], color, d.FogColor};
        if (d.Masks)
            inputs.insert(inputs.end(), {d.Bindings[11], d.Bindings[12], d.Masks});
        if (!d.FogPipeline)
        {
            static const uint32_t code[] =
#include "Shaders/Fog.comp.inc"
                ;
            static const uint32_t masked[] =
#include "Shaders/FogAlphaMask.comp.inc"
                ;
            d.FogPipeline = d.Device.CreatePipeline(d.Masks ? masked : code, d.Masks ? sizeof(masked) : sizeof(code),
                std::vector<ComputeBinding>(inputs.size(), ComputeBinding::Buffer), sizeof(FogSettings));
        }
        d.Device.Dispatch(d.FogPipeline, inputs, &fog, uint32_t((pixels + 63) / 64));
        return d.FogColor;
    }
    const glm::mat4 &GlobalIllumination::GetViewProjection() const
    {
        return m_Data->ViewProjection;
    }
    glm::vec3 GlobalIllumination::GetEye() const
    {
        return glm::vec3(m_Data->Info.Eye);
    }
    const std::vector<GILight> &GlobalIllumination::GetLights() const
    {
        return m_Data->Lights;
    }
    bool GlobalIllumination::UsesDiffuseReference() const { return m_Data->Info.GridOrigin.w != 0.f; }
    const Ref<ComputeResource>& GlobalIllumination::GetSceneNodes() const { return m_Data->Bindings[0]; }
    const Ref<ComputeResource>& GlobalIllumination::GetSceneTriangles() const { return m_Data->Bindings[1]; }

    GIReflectionResources GlobalIllumination::GetReflectionResources() const
    {
        const auto& d = *m_Data;
        const auto& buffers = d.Bindings;
        return {buffers[0], buffers[1], buffers[3], buffers[9], buffers[11], buffers[12],
                d.Emitters ? d.Emitters : d.Empty,
                HasBakedProbes() || d.ProbeFrame > 0 ? buffers[4] : nullptr, d.Masks};
    }

    bool GlobalIllumination::SetProbeFeedback(bool enabled)
    {
        auto& d = *m_Data;
        if (d.ProbeFeedback == enabled) return false;
        d.ProbeFeedback = enabled;
        d.ProbeFrame = 0;
        d.BakedProbes.clear();
        ResetHistory();
        return true;
    }

    void GlobalIllumination::BakeProbes(const std::filesystem::path& file)
    {
        auto& d = *m_Data;
        if (!d.UnreadableTexture.empty())
            throw std::runtime_error("烘焙需要的贴图无法读取：" + d.UnreadableTexture.u8string());

        // 烘焙时继续用旧探针照亮场景。新算出的光先单独存放，等文件写好再换上去。
        std::vector<glm::vec4> values(d.ProbeFaces() * 2);
        const size_t bytes = values.size() * sizeof(values[0]);
        std::vector<Ref<ComputeResource>> buffers = d.Bindings;
        buffers[4] = d.Device.CreateBuffer(bytes);
        const std::vector<GILight> lights = d.BakeLights();
        if (lights.size() > 1)
            buffers[3] = d.Device.CreateBuffer(lights.size() * sizeof(GILight), lights.data());

        // 第一轮记录墙面反射的直接光和材质自身发出的光。开启多次反射后，每一轮还会算入墙面从上轮探针收到的光。
        const uint32_t rounds = d.ProbeFeedback ? Data::BakeRounds : 1u;
        for (uint32_t round = 0; round < rounds; ++round)
            d.UpdateProbes(buffers, round, Data::BakeRays, false);
        d.Device.Readback(buffers[4], values.data(), bytes);
        ProbeBake::Save(file, d.ProbeKey(lights), values);

        d.Bindings[4] = std::move(buffers[4]);
        d.Bindings[13] = std::move(buffers[13]);
        d.BakedProbes = std::move(values);
        ResetHistory();
    }

    bool GlobalIllumination::LoadProbes(const std::filesystem::path& file)
    {
        auto& d = *m_Data;
        std::vector<glm::vec4> values(d.ProbeFaces() * 2);
        if (!d.UnreadableTexture.empty() || !ProbeBake::Load(file, d.ProbeKey(d.BakeLights()), values))
            return false;
        d.Device.Upload(d.Bindings[4], values.data(), values.size() * sizeof(values[0]));
        d.BakedProbes = std::move(values);
        ResetHistory();
        return true;
    }

    void GlobalIllumination::SaveProbes(const std::filesystem::path& file) const
    {
        const auto& d = *m_Data;
        if (!HasBakedProbes())
            throw std::logic_error("Bake probes before saving them");
        ProbeBake::Save(file, d.ProbeKey(d.BakeLights()), d.BakedProbes);
    }

    bool GlobalIllumination::HasBakedProbes() const { return !m_Data->BakedProbes.empty(); }

    void GlobalIllumination::RenderDirect(bool useReSTIR)
    {
        auto &d = *m_Data;
        size_t pixels = size_t(d.Info.ImageSize.x) * d.Info.ImageSize.y;
        if (!d.DirectOutput)
            d.DirectOutput = d.Device.CreateBuffer(pixels * sizeof(glm::vec4));
        std::swap(d.Bindings[8], d.DirectOutput);
        if (useReSTIR)
        {
            d.PrepareReservoirs(false);
            if (!d.DirectHistory[0])
            {
                for (auto &h : d.DirectHistory)
                    h = d.Device.CreateBuffer(pixels * 4 * sizeof(glm::vec4));
            }
            std::swap(d.DirectFrame, d.Frame);
            std::swap(d.Bindings[5], d.DirectHistory[0]);
            std::swap(d.Bindings[7], d.DirectHistory[1]);
            d.Dispatch(5);
            d.Dispatch(7);
            std::swap(d.Bindings[5], d.DirectHistory[0]);
            std::swap(d.Bindings[7], d.DirectHistory[1]);
            std::swap(d.DirectHistory[0], d.DirectHistory[1]);
            ++d.Frame;
            std::swap(d.DirectFrame, d.Frame);
        }
        else
        {
            d.Dispatch(3);
            d.DirectFrame = 0;
        }
        std::swap(d.Bindings[8], d.DirectOutput);
    }

    void GlobalIllumination::Render(GIMethod method, uint32_t samples)
    {
        auto &d = *m_Data;
        if (!samples)
            throw std::invalid_argument("A GI estimate requires at least one sample");
        if (d.Method != method)
        {
            ResetHistory();
            d.Method = method;
        }
        switch (method)
        {
        case GIMethod::DDGI:
            if (!HasBakedProbes())
                d.UpdateProbes(d.Bindings, d.ProbeFrame++, samples, true);
            d.Dispatch(10);
            break;
        case GIMethod::ReSTIRDI:
        case GIMethod::ReSTIRGI: {
            const bool gi = method == GIMethod::ReSTIRGI;
            d.PrepareReservoirs(true);
            d.Dispatch(gi ? 6 : 5);
            d.Dispatch(gi ? 8 : 7);
            std::swap(d.Bindings[5], d.Bindings[7]);
            break;
        }
        case GIMethod::DirectReference:
            d.Dispatch(3);
            break;
        default: {
            uint32_t stage = method == GIMethod::Direct ? 1 : (method == GIMethod::Indirect ? 2 : 4);
            for (uint32_t i = 0; i < samples; i += 4)
                d.Dispatch(stage, std::min(4u, samples - i), i);
            break;
        }
        }
        ++d.Frame;
    }
} // namespace Redflag
