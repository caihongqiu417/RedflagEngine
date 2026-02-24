#include "rfpch.h"
#include "ParticleSystem.h"

#include <random>
#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/compatibility.hpp>

#include"Redflag/Renderer/Renderer2D.h"

namespace Redflag
{
	//===============随机数生成器================
	static std::mt19937 s_RandomEngine;
	static std::uniform_int_distribution<std::mt19937::result_type> s_Distribution;

	static bool init = []() {
		s_RandomEngine.seed(std::random_device()());
		return true;
		}();

	static float RandomFloat() {
		return (float)s_Distribution(s_RandomEngine) / (float)std::numeric_limits<uint32_t>::max();
	}
	//========================================================

	ParticleSystem::ParticleSystem(uint32_t maxParticles)
		:m_MaxParticles(maxParticles - 1)
	{
		m_ParticlePool.resize(m_MaxParticles);
		m_PoolIndex = 0;

		// 确保所有粒子初始都是不活跃的
		for (auto& particle : m_ParticlePool)
		{
			particle.Active = false;
		}
	}

	void ParticleSystem::OnUpdate(Redflag::Timestep ts)
	{
		for (auto& particle : m_ParticlePool)
		{
			if (!particle.Active)
				continue;

			if (particle.LifeRemaining <= 0.0f)
			{
				particle = Particle();  // 直接重置为全新的空粒子
				continue;
			}

			particle.LifeRemaining -= ts;
			particle.Position += particle.Velocity * (float)ts;
		}
	}

	void ParticleSystem::OnRender(OrthographicCamera& camera)
	{
		Renderer2D::BeginScene(camera);

		for (auto& particle : m_ParticlePool)
		{
			if (!particle.Active)
				continue;

			float life = particle.LifeRemaining / particle.LifeTime;

			// 1. 生命值太低就不渲染
			if (life < 0.001f)
				continue;

			glm::vec4 color = glm::lerp(particle.ColorEnd, particle.ColorBegin, life);
			float size = glm::lerp(particle.SizeEnd, particle.SizeBegin, life);

			// 2. 颜色太透明就不渲染
			if (color.a < 0.01f)
				continue;

			// 3. 大小太小就不渲染（根据你的SizeEnd=0.02来调整）
			if (size < 0.01f)
				continue;

			Renderer2D::DrawRotatedQuad(
				{ particle.Position.x, particle.Position.y, 0.0f },
				{ size, size },
				particle.Rotation,
				color
			);
		}

		Renderer2D::EndScene();
	}

	void ParticleSystem::Emit(const ParticleProps& particleProps)
	{
		// 从粒子池中取出一个粒子
		Particle& particle = m_ParticlePool[m_PoolIndex];

		// 先重置为不活跃（确保清除旧状态）
		particle.Active = false;

		// 设置新属性
		particle.Active = true;
		particle.Position = particleProps.Position;
		particle.Rotation = RandomFloat() * 2.0f * glm::pi<float>();

		// 速度
		particle.Velocity = particleProps.Velocity;
		particle.Velocity.x += particleProps.VelocityVariation.x * (RandomFloat() - 0.5f);
		particle.Velocity.y += particleProps.VelocityVariation.y * (RandomFloat() - 0.5f);

		// 颜色
		particle.ColorBegin = particleProps.ColorBegin;
		particle.ColorEnd = particleProps.ColorEnd;

		// 生命周期
		particle.LifeTime = particleProps.LifeTime;
		particle.LifeRemaining = particleProps.LifeTime;

		// 大小
		particle.SizeBegin = particleProps.SizeBegin + particleProps.SizeVariation * (RandomFloat() - 0.5f);
		particle.SizeEnd = particleProps.SizeEnd;

		// 环形递增索引
		m_PoolIndex = (m_PoolIndex + 1) % m_ParticlePool.size();
	}

	void ParticleSystem::Clear()
	{
		for (auto& particle : m_ParticlePool)
		{
			particle.Active = false;
		}
		m_PoolIndex = 0;
	}
}
