#pragma once

#include "Redflag/Renderer/EditorCamera.h"
#include "Redflag/Renderer/Mesh.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Renderer/Material.h"
#include "Redflag/Renderer/Texture.h"
#include "Redflag/Renderer/Shader.h"

#include <glm/glm.hpp>

namespace Redflag
{
	/*
	 * 先收集要画的网格，再把能一起画的对象合批，其余按材质排序后统一绘制。
	 * BeginScene 设置相机，DrawMesh/DrawModel 将网格和材质加入队列，EndScene 绘制主画面。
	 * 阴影和反射阴影图沿用完整队列，画面外的物体也能影响画面内的光照。
	 * 主帧缓冲由调用方持有，各 Pass 的执行顺序由 SceneRenderer 安排。
	 */
	class Renderer3D
	{
	public:
			struct Statistics
			{
				uint32_t DrawCalls = 0;
				uint32_t SubmittedCommands = 0;
				uint32_t VertexCount = 0;
				uint32_t IndexCount = 0;
				uint32_t CulledCount = 0;  // 主相机视野外的网格数
			};

			// 单次 DrawCall 的简略信息，供 ImGui Profile 面板展示
			struct DrawCallInfo
			{
				enum class PassType { Main, Shadow, Prepass, SSAO, RSM, BloomBright, BloomUp, BloomTonemap };
				PassType Pass       = PassType::Main;
				uint32_t IndexCount = 0;
				int      EntityID   = -1;
				int      AlphaMode  = 0;   // 0=Opaque 1=Mask 2=Blend
				uintptr_t MaterialKey = 0; // 同一 key 表示同材质
				uintptr_t MeshPtr   = 0;   // 当帧 Mesh 对象地址，仅用于调试区分
			};
			static const std::vector<DrawCallInfo>& GetDrawCallLog();

			// 全屏绘制也计入性能面板里的绘制次数。
			static void LogExternalDrawCall(DrawCallInfo::PassType pass, uint32_t indexCount);

		// ---- 初始化与销毁 ----
		static void Init();
		static void Shutdown();

		// ---- 帧与提交 ----
		// viewProjection=projection*view，cameraPosition 为世界坐标。
		static void BeginScene(const EditorCamera& camera);
		static void BeginScene(const glm::mat4& viewProjection, const glm::vec3& cameraPosition = glm::vec3(0.0f), const glm::mat4& view = glm::mat4(1.0f));

		static void EndScene();

		// 把网格加入本帧队列。transform 将顶点从模型空间变到世界空间，entityID 用于鼠标拾取。
		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const glm::vec4& color = glm::vec4(1.0f), int entityID = -1);
		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<Texture2D>& texture, int entityID = -1);
		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const Ref<Material>& material, int entityID = -1);

		// 子网格先按模型节点的变换摆放，再随整个实体移动。
		static void DrawModel(const Ref<Model>& model, const glm::mat4& transform, const glm::vec4& color = glm::vec4(1.0f), int entityID = -1);
		static void DrawModel(const Ref<Model>& model, const glm::mat4& transform, const std::vector<Ref<Material>>& overrideMaterials, const glm::vec4& fallbackColor, int entityID = -1);

		// ---- 阴影、反射阴影图与深度预写 ----
		// 这些绘制共用已提交的网格。调用方先选好要写入的图像和视口。
		// lightSpaceMatrix 将世界坐标变到光源裁剪空间。shadowParams 依次保存两项深度偏移、图宽的倒数和阴影模式。
		// shadowParams2 的前两项是采样半径与光源大小，按阴影图像素计。后两项沿用主视口宽高。
		static void SetShadowData(const glm::mat4& lightSpaceMatrix,
			const glm::vec4& shadowParams,
			const glm::vec4& shadowParams2 = glm::vec4(1.5f, 6.0f, 0.0f, 0.0f));
		// 从光源方向写深度，镂空材质沿用主画面的裁剪条件，半透明材质跳过。
		static void ExecuteShadowPass(const Ref<Shader>& depthShader);

		// 记录光源照到的表面位置、朝向，以及表面向外反射多少漫射光。
		static void ExecuteRSMPass(const Ref<Shader>& rsmShader);

		// 提交完网格、绘制主画面之前，先生成主相机的深度和观察空间法线。
		static void ExecuteDepthPrepass(const Ref<Shader>& depthShader);

		// 临时切换主绘制的环境贴图光照。
		static void OverrideIBLFlag(bool enabled);

		// ---- 剔除与调试 ----
		static bool& GetFrustumCullingEnabled();

		// 用包围盒检查剔除结果。绿色表示与相机视野相交，红色表示在视野外。
		static bool& GetDebugCullingVizEnabled();

		// 用 Renderer2D 绘制当前帧收集到的 AABB 线框（需要先 Renderer2D::BeginScene）
		static void DrawDebugAABBs();

		// ---- 统计 ----
		static void ResetStats();
		static Statistics GetStats();
	};
}
