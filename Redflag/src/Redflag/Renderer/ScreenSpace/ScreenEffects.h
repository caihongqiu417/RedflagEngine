#pragma once

#include "Redflag/Renderer/RHI/ComputeDevice.h"
#include <glm/glm.hpp>

namespace Redflag
{
    class GlobalIllumination;

    enum class ScreenEffect
    {
        GTAO,
        ClusteredLighting,
        StochasticReflections,
        TAA,
        EnvironmentDiffuse,
        MaterialReflections
    };

    struct ScreenEffectSettings
    {
        float AORadius = 0.65f, AOIntensity = 1.0f;
        float Roughness = 0.2f, ReflectionDistance = 12.0f;
        // 设为 0 只显示当前帧。开启后，固定镜头下的轮廓按采样次数混色，其他像素最多保留这个比例的旧颜色。
        float HistoryWeight = 0.9f;
        float LightRange = 4.0f; // 漫反射对照测试中各灯共用的照明半径，按场景长度计。编辑器读取每盏灯自己的半径。
        bool TraceReflections = true, SpecularEnvironment = true;
        bool TraceScene = false; // SSR 在画面里找不到物体时，继续查场景里的三角形。
        int ReflectionBounces = 1; // 1 表示从眼前的镜面反射一次。取 2 时还能看到第二面镜子照见的物体。
        bool DiffuseEnvironment = true, ReflectProbeLight = false;
        bool DenoiseReflections = false; // 用附近同一表面的倒影减轻粗糙材质上的噪点。
    };

    /* 每个视口为 TAA 保存上一帧的颜色和表面位置。换场景时调用 ResetHistory，下一帧只显示新场景的颜色。 */
    class ScreenEffects
    {
    public:
        ScreenEffects(ComputeDevice& device, uint32_t width, uint32_t height);
        ~ScreenEffects();

        /// <summary>
        /// 对当前视口运行一种屏幕效果，返回的结果可以接着交给下一种效果。
        /// </summary>
        /// <param name="inputColor">上一种效果算出的颜色。留空时使用 lighting 当前的输出。</param>
        /// <param name="stableProjection">开启 TAA 时传入相机原本的投影矩阵。即使每帧在像素里取样的位置不同，也能判断镜头有没有移动。</param>
        Ref<ComputeResource> Render(ScreenEffect effect, const GlobalIllumination& lighting,
                                    const glm::mat4& projection, const glm::mat4& view, uint32_t frame,
                                    const Ref<ComputeResource>& inputColor = {},
                                    const glm::mat4* stableProjection = nullptr);

        ScreenEffectSettings& GetSettings();
        void SetEnvironment(const Ref<ComputeResource>& lighting);
        void ResetHistory();

        /// <summary>每帧略微挪动像素的采样点。用返回的投影矩阵绘制场景，再把它传给 Render。</summary>
        static glm::mat4 JitterProjection(const glm::mat4& projection, uint32_t frame, uint32_t width, uint32_t height);

    private:
        struct Data;
        Scope<Data> m_Data;
    };
}
