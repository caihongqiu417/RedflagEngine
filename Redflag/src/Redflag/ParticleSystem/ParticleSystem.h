#pragma once

#include"Redflag/Renderer/OrthographicCameraController.h"

namespace Redflag
{
	// 粒子属性：发射粒子时需要的参数
	struct ParticleProps
	{
		glm::vec2 Position;              // 出生位置
		glm::vec2 Velocity;              // 基础速度
		glm::vec2 VelocityVariation;      // 速度随机变化范围
		glm::vec4 ColorBegin;             // 起始颜色
		glm::vec4 ColorEnd;               // 结束颜色（随生命渐变）
		float SizeBegin;                  // 起始大小
		float SizeEnd;                    // 结束大小
		float SizeVariation;              // 大小随机变化范围
		float LifeTime = 1.0f;            // 存活时间（秒）
	};

	// 粒子系统：管理所有粒子的更新和渲染
	class ParticleSystem
	{
	public:
		ParticleSystem(uint32_t maxParticles = 100000);

		// 每帧更新：移动粒子、减少生命值
		void OnUpdate(Timestep ts);

		// 每帧渲染：绘制所有活跃粒子
		void OnRender(OrthographicCamera& camera);

		// 渲染到当前已开启的 2D 批次中（不调用 BeginScene/EndScene）
		void RenderBatch();

		// 发射一个新粒子
		void Emit(const ParticleProps& particleProps);

		// 清空所有粒子
		void Clear();

	private:
		// 单个粒子的数据结构
		struct Particle
		{
			glm::vec2 Position;           // 当前位置
			glm::vec2 Velocity;           // 当前速度
			glm::vec4 ColorBegin;          // 起始颜色
			glm::vec4 ColorEnd;            // 结束颜色
			float Rotation = 0.0f;         // 旋转角度
			float SizeBegin;                // 起始大小
			float SizeEnd;                  // 结束大小

			float LifeTime = 1.0f;         // 总生命时间
			float LifeRemaining = 0.0f;    // 剩余生命时间

			bool Active = false;           // 是否活跃（true=正在使用）
		};

		std::vector<Particle> m_ParticlePool;  // 粒子池（固定大小，循环使用）
		uint32_t m_PoolIndex = 0;            // 当前要用的粒子索引（循环索引）
		uint32_t m_MaxParticles;             // 粒子池的最大容量
	};
}

