#pragma once

#include<glm/glm.hpp>
#include<glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include<glm/gtx/quaternion.hpp>
#include"SceneCamera.h"
#include "Redflag/Renderer/SubTexture2D.h"
#include"Redflag/Renderer/Texture.h"
#include"Redflag/Core/Timestep.h"
#include"Redflag/Core/UUID.h"
#include"Redflag/Renderer/Mesh.h"
#include"Redflag/Renderer/Model.h"

namespace Redflag {

	struct IDComponent
	{
		UUID ID;

		IDComponent() = default;
		IDComponent(const IDComponent&) = default;
	};

	struct TransformComponent
	{
		glm::vec3 Translation = { 0.0f,0.0f,0.0f };
		glm::vec3 Rotation = { 0.0f,0.0f,0.0f };
		glm::vec3 Scale = { 1.0f,1.0f,1.0f };

		TransformComponent() = default;
		TransformComponent(const TransformComponent& other) = default;  //Ĭ�Ͽ�������Ϊ��Աû��ָ�룬����ǳ��������
		TransformComponent(const glm::vec3& translation)
			:Translation(translation){}

		glm::mat4 GetTransform()const
		{
			glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));

			return glm::translate(glm::mat4(1.0f), Translation) * rotation * glm::scale(glm::mat4(1.0f), Scale);
		}
	};

	struct SpriteRendererComponent
	{
		glm::vec4 Color = glm::vec4(1.0f);
		Ref<Texture2D> Texture;
		float TilingFactor = 1.f;

		SpriteRendererComponent() = default;
		SpriteRendererComponent(const SpriteRendererComponent& other) = default;
		SpriteRendererComponent(const glm::vec4& color)
			:Color(color){}
	};

	struct CircleRendererComponent
	{
		glm::vec4 Color{ 1.1f,1.f,1.f,1.f };
		float Thickness = 1.f;
		float Fade = 0.005f;

		CircleRendererComponent() = default;
		CircleRendererComponent(const CircleRendererComponent&) = default;
	};

	struct TagComponent
	{
		std::string Tag;

		TagComponent() = default;
		TagComponent(const TagComponent& other) = default;
		TagComponent(const std::string& tag)
			:Tag(tag){}
	};

	struct CameraComponent
	{
		SceneCamera Camera;
		bool Primary = true;
		bool FixedAspectRatio = false; //Ĭ�ϲ��ã�ֻ���ӿ������Ҫ�ı���߱�

		CameraComponent() = default;
		CameraComponent(const CameraComponent& other) = default;
	};

	class ScriptableEntity;

	struct NativeScriptComponent
	{
		ScriptableEntity* Instance = nullptr;

		// ����ָ��
		ScriptableEntity* (*InstantiateScript)();  
		void (*DestroyScript)(NativeScriptComponent*);

		template<typename T>
		void Bind()
		{
			// lambda���뺯��ָ�� - ʹ����ȷ�ı�����
			InstantiateScript = []() -> ScriptableEntity* {
				return static_cast<ScriptableEntity*>(new T());
				};

			DestroyScript = [](NativeScriptComponent* nsc) {
				delete static_cast<ScriptableEntity*>(nsc->Instance);
				nsc->Instance = nullptr;
				};
		}
	};

	//����
	struct RigidBody2DComponent
	{
		enum class BodyType{Static=0,Dynamic,Kinematic};
		BodyType Type = BodyType::Static;
		bool FixedRotation = false;

		//�洢Box2D��Ϣ
		void* RuntimeBody = nullptr;

		RigidBody2DComponent() = default;
		RigidBody2DComponent(const RigidBody2DComponent&) = default;
	};

	struct BoxCollider2DComponent
	{
		glm::vec2 Offset = { 0.f,0.f };
		glm::vec2 Size = { 0.5f,0.5f };

		//TODO��������������
		float Density = 1.f;
		float Friction = 0.5f;
		float Restitution = 0.f;
		float RestitutionThreshold = 0.5f; //ֻ�е������� �����ײ�ٶ� > 0.5 ��/�� ʱ���������������õĵ��Լ��㡣

		//�洢Box2D��Ϣ
		void* RuntimeFixture = nullptr;

		BoxCollider2DComponent() = default;
		BoxCollider2DComponent(const BoxCollider2DComponent&) = default;
	};

	// 2D 圆形碰撞体组件
	// 对应 Box2D 的 b2CircleShape，用于圆形刚体的碰撞检测
	struct CircleCollider2DComponent
	{
		// 碰撞圆心相对于实体 Transform 原点的偏移（局部空间，单位与场景一致）
		glm::vec2 Offset = { 0.f, 0.f };

		// 碰撞圆的半径（局部空间，默认 0.5）
		float Radius = 0.5f;

		// 密度（kg/m²），影响刚体的质量；设为 0 时视为传感器（无质量）
		float Density = 1.f;

		// 摩擦系数 [0, 1]，0 = 完全光滑，1 = 最大摩擦
		float Friction = 0.5f;

		// 恢复系数（弹性）[0, 1]，0 = 完全非弹性碰撞，1 = 完全弹性碰撞
		float Restitution = 0.f;

		// 恢复生效的最低碰撞速度阈值（m/s）；低于此速度时不应用弹性，避免微小抖动
		float RestitutionThreshold = 0.5f;

		// 运行时由 Box2D 填充的 b2Fixture* 指针，序列化时忽略
		void* RuntimeFixture = nullptr;

		CircleCollider2DComponent() = default;
		CircleCollider2DComponent(const CircleCollider2DComponent&) = default;
	};

	// 3D 网格渲染组件
	struct MeshRendererComponent
	{
		Ref<Mesh>      MeshData;
		glm::vec4      Color = glm::vec4(1.0f);
		Ref<Texture2D> Texture;

		MeshRendererComponent() = default;
		MeshRendererComponent(const MeshRendererComponent&) = default;
	};

	// 3D 模型渲染组件（通过 Assimp 从文件加载，支持多子网格）
	struct ModelRendererComponent
	{
		std::string  FilePath;       // 模型文件路径，场景序列化时保存此字段
		Ref<Model>   ModelAsset;     // 运行时句柄，由 AssetManager 管理，不序列化

		glm::vec4    Color = glm::vec4(1.0f); // 无贴图时的备用颜色

		ModelRendererComponent() = default;
		ModelRendererComponent(const ModelRendererComponent&) = default;
	};

	// ---------------------------------------------------------------
	// 方向光组件 — 模拟无限远的平行光源（太阳光）
	//
	// Direction 是光线射出的方向（世界空间），shader 中通常取反
	// 得到「从表面指向光源」的向量用于 NdotL 计算。
	//
	// CastShadow 标记该灯是否参与 Shadow Map 生成，
	// 阶段 5 实现 Shadow Pass 时会读取这个标志。
	// ---------------------------------------------------------------
	struct DirectionalLightComponent
	{
		glm::vec3 Direction = { 0.0f, -1.0f, 0.0f }; // 默认从正上方往下照
		glm::vec3 Color     = { 1.0f, 1.0f, 1.0f };  // 白光
		float     Intensity = 1.0f;                    // 辐照度缩放因子
		bool      CastShadow = true;                   // 是否投射阴影

		DirectionalLightComponent() = default;
		DirectionalLightComponent(const DirectionalLightComponent&) = default;
	};

	// ---------------------------------------------------------------
	// 点光源组件 — 从一个点向四周均匀辐射
	//
	// 位置由实体的 TransformComponent 决定，这里不重复存储。
	// Radius 是光照影响范围，超出此距离的片元跳过该灯的计算
	// （衰减降为零），这是性能优化的关键参数。
	//
	// 衰减公式（后续 shader 中使用）：
	//   attenuation = clamp(1.0 - (distance / Radius)^2, 0, 1)^2
	// 这是 UE4 风格的平滑衰减，避免硬截断。
	// ---------------------------------------------------------------
	struct PointLightComponent
	{
		glm::vec3 Color     = { 1.0f, 1.0f, 1.0f };
		float     Intensity = 1.0f;
		float     Radius    = 10.0f;  // 影响半径（世界空间单位）
		bool      CastShadow = false;  // 点光阴影（立方体 Shadow Map），默认关闭因为开销较大

		PointLightComponent() = default;
		PointLightComponent(const PointLightComponent&) = default;
	};
}