#pragma once

#include "Redflag/Assets/Asset.h"
#include "Redflag/Gameplay/GameplayComponents.h"
#include "Redflag/AI/NpcComponent.h"
#include "Redflag/Scene/EnvironmentZoneComponent.h"
#include "Redflag/Scene/DecalComponent.h"
#include "Redflag/Terrain/TerrainComponent.h"
#include "Redflag/Animation/ModelAnimationPlayer.h"
#include "Redflag/Animation/SkeletonAnimation.h"
#include "Redflag/Physics/PhysicsWorld.h"

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
#include"Redflag/Renderer/Material.h"

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
		TransformComponent(const TransformComponent& other) = default;  //默认拷贝，因为成员没有指针，所以浅拷贝即可
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
		bool FixedAspectRatio = false;

		CameraComponent() = default;
		CameraComponent(const CameraComponent& other) = default;
	};

	struct ScriptComponent
	{
		using FieldMap = std::unordered_map<std::string, std::vector<uint8_t>>;
		std::string ClassName;
		FieldMap Fields; // 场景中填写的初值；播放副本和编辑场景各持一份。

		ScriptComponent() = default;
		ScriptComponent(const ScriptComponent& other) = default;
	};

	class ScriptableEntity;

	struct NativeScriptComponent
	{
		ScriptableEntity* Instance = nullptr;

		// 函数指针
		ScriptableEntity* (*InstantiateScript)();  
		void (*DestroyScript)(NativeScriptComponent*);

		template<typename T>
		void Bind()
		{
			// lambda存入函数指针 - 使用正确的变量名
			InstantiateScript = []() -> ScriptableEntity* {
				return static_cast<ScriptableEntity*>(new T());
				};

			DestroyScript = [](NativeScriptComponent* nsc) {
				delete static_cast<T*>(nsc->Instance);
				nsc->Instance = nullptr;
				};
		}
	};


	//物理
	struct RigidBody2DComponent
	{
		enum class BodyType{Static=0,Dynamic,Kinematic};
		BodyType Type = BodyType::Static;
		bool FixedRotation = false;

		RigidBody2DComponent() = default;
		RigidBody2DComponent(const RigidBody2DComponent&) = default;
	};

	struct BoxCollider2DComponent
	{
		glm::vec2 Offset = { 0.f,0.f };
		glm::vec2 Size = { 0.5f,0.5f };

		//TODO：放入物理材质
		float Density = 1.f;
		float Friction = 0.5f;
		float Restitution = 0.f;
		float RestitutionThreshold = 0.5f; //碰撞时取两个物体设置中较小的值。沿接触面法线靠近得比它快，才计算反弹（米/秒）。

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

		// 密度（kg/m²）。设为 0 时，这个圆仍能碰撞，但它的面积不再计入刚体质量。
		float Density = 1.f;

		// 沿接触面滑动时的摩擦系数。编辑器可调 0 到 1，设为 0 时可以自由滑动。
		float Friction = 0.5f;

		// 恢复系数（弹性）[0, 1]，0 = 完全非弹性碰撞，1 = 完全弹性碰撞
		float Restitution = 0.f;

		// 两个碰撞体取各自设置中较小的值。沿接触面法线相互靠近的速度超过它时，Box2D 才计算反弹（米/秒）。
		float RestitutionThreshold = 0.5f;

		CircleCollider2DComponent() = default;
		CircleCollider2DComponent(const CircleCollider2DComponent&) = default;
	};

	struct RigidBody3DComponent
	{
		BodyType3D Type = BodyType3D::Static;
		float Density = 1.0f; // kg/m³。动态刚体的质量等于密度乘以碰撞盒的体积。
	};

	struct BoxCollider3DComponent
	{
		glm::vec3 HalfSize{ 0.5f }; // 从中心到各面的距离，单位为米，场景缩放另算。
		glm::vec3 Center{0}; // 角色模型的原点常在脚底，身体碰撞盒的中心在它上方。
	};

	/* 带此标记的实体允许被拿起。碰撞范围由它的碰撞体决定。 */

	/* 变换的位置是骨盆中心。运行时的七个碰撞盒和关节由场景持有。 */
	struct RagdollComponent
	{
		bool Walking = true;
		float Speed = .8f; // 米/秒。负值后退，零让角色站住。
		Ref<Mesh> LimbMesh;
		Ref<Material> LimbMaterial;
	};

	// 3D 网格渲染组件
	struct MeshRendererComponent
	{
		Ref<Mesh>      MeshData;
		glm::vec4      Color = glm::vec4(1.0f);
		Ref<Texture2D> Texture;
		Ref<Material>  Mat;   // 指定材质时用它绘制，否则使用上面的颜色和贴图。

		MeshRendererComponent() = default;
		MeshRendererComponent(const MeshRendererComponent&) = default;
	};

	struct ModelLOD
	{
		AssetID ModelID = 0;
		std::string FilePath;
		float ScreenHeight = .25f; // 0.25 表示模型大约占场景画面高度的四分之一时用这一档。
		Ref<Model> ModelAsset;
	};

	// 3D 模型渲染组件（通过 Assimp 从文件加载，支持多子网格）
	struct ModelRendererComponent
	{
		AssetID ModelID = 0;
		std::string FilePath; // 旧场景兼容与编辑器显示。
		Ref<Model>   ModelAsset;     // 运行时句柄，由 AssetManager 管理，不序列化

		glm::vec4    Color = glm::vec4(1.0f); // 无贴图时的备用颜色
		std::vector<Ref<Material>> OverrideMaterials; // per-submesh 覆盖材质（可选）
		std::vector<ModelLOD> LODs; // LOD 0 用原模型，下面依次保存由近到远的简化模型。
		size_t ActiveLOD = 0;

		ModelRendererComponent() = default;
		ModelRendererComponent(const ModelRendererComponent&) = default;
	};

	// ----------
	// 方向光的光线彼此平行。Direction 指向光照过去的方向，计算表面迎光的角度时取反。
	// ----------
	struct DirectionalLightComponent
	{
		glm::vec3 Direction = { 0.0f, -1.0f, 0.0f }; // 默认从正上方往下照
		glm::vec3 Color     = { 1.0f, 1.0f, 1.0f };  // 白光
		float     Intensity = 1.0f;                    // 灯光亮度的倍数
		bool      CastShadow = true;                   // 是否投射阴影

		DirectionalLightComponent() = default;
		DirectionalLightComponent(const DirectionalLightComponent&) = default;

		glm::vec3 WorldDirection(const TransformComponent& transform) const
		{
			// 旋转灯会改变光照方向。移动或缩放灯的图标不会改变太阳光。
			return glm::quat(transform.Rotation) * Direction;
		}
	};

	// ----------
	// 点光源从实体所在的位置向四周照。设定照明半径后，表面越接近半径边缘，收到的灯光就越弱。
	// ----------
	struct PointLightComponent
	{
		glm::vec3 Color     = { 1.0f, 1.0f, 1.0f };
		float     Intensity = 1.0f;
		float     Radius    = 10.0f;  // 影响半径（世界空间单位）
		bool      CastShadow = false;  // 随场景保存。当前点光源着色器没有按此开关计算阴影。

		PointLightComponent() = default;
		PointLightComponent(const PointLightComponent&) = default;
	};

	// ----------
	// 2D 粒子发射器组件
	//
	// 将 ParticleSystem 的粒子属性包装为 ECS 组件，附加到实体后
	// 以 EmissionRate 速率从实体 Transform 位置持续发射粒子。
	// ----------
	struct ParticleEmitterComponent
	{
		// --- 粒子外观 ---
		glm::vec4 ColorBegin = { 0.4f, 0.7f, 1.0f, 1.0f };
		glm::vec4 ColorEnd   = { 1.0f, 0.5f, 0.8f, 0.0f };

		// --- 粒子大小 ---
		float SizeBegin     = 0.1f;
		float SizeEnd       = 0.02f;
		float SizeVariation = 0.03f;

		// --- 粒子运动 ---
		glm::vec2 Velocity          = { 0.0f, 0.0f };
		glm::vec2 VelocityVariation = { 2.0f, 2.0f };

		// --- 生命周期 ---
		float LifeTime = 1.8f;

		// --- 发射控制 ---
		float    EmissionRate = 10.0f;   // 每秒发射粒子数
		uint32_t MaxParticles = 1000;    // 粒子池大小
		bool     Active       = true;    // 是否正在发射

		// --- 运行时数据（不序列化）---
		void* RuntimeParticleSystem   = nullptr;
		float EmissionAccumulator     = 0.0f;

		ParticleEmitterComponent() = default;
		ParticleEmitterComponent(const ParticleEmitterComponent&) = default;
	};

	// ----------
	// 2D 纹理动画组件（精灵表帧动画）
	//
	// 配合 SpriteRendererComponent 使用：Sprite 上的纹理作为精灵表，
	// 此组件控制每帧显示哪一帧子纹理。
	// ----------
	struct TextureAnimationComponent
	{
		int   Columns        = 1;       // 精灵表列数
		int   Rows           = 1;       // 精灵表行数
		int   FrameCount     = 1;       // 动画总帧数（可 <= Columns * Rows）
		float AnimationSpeed = 10.0f;   // 帧率（帧/秒）
		bool  Loop           = true;    // 是否循环播放

		// --- 运行时状态（不序列化）---
		int   CurrentFrame = 0;
		float Timer        = 0.0f;

		TextureAnimationComponent() = default;
		TextureAnimationComponent(const TextureAnimationComponent&) = default;
	};
}
