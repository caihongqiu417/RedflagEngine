#include "rfpch.h"
#include "Foliage.h"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <random>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
	std::vector<glm::mat4> Foliage::Place(double seconds, bool windy) const
	{
		std::mt19937 random(Seed);
		// 使用高 24 位生成 [0, 1) 的位置。相同的种子会在不同帧得到相同的草根。
		auto fraction = [&]() { return float(random() >> 8) / float(1u << 24); };
		const glm::vec2 dir = Direction == glm::vec2(0) ? glm::vec2(0) : glm::normalize(Direction);
		std::vector<glm::mat4> transforms;
		transforms.reserve(Count);
		for (uint32_t i = 0; i < Count; ++i)
		{
			const float x = (fraction() - .5f) * Size.x;
			const float z = (fraction() - .5f) * Size.y;
			const float yaw = glm::two_pi<float>() * fraction();
			const double phase = glm::two_pi<double>() * (Frequency * seconds - glm::dot(dir, glm::vec2(x, z)) / Wavelength);
			const glm::vec2 slope = windy ? dir * (Lean * float(std::sin(phase))) : glm::vec2(0);

			// 草根处 y=Root，水平位移为零。比根高一米的叶片移动 slope 米。
			glm::mat4 shear(1.f);
			shear[1] = {slope.x, 1.f, slope.y, 0.f};
			shear[3] = {x - slope.x * Scale * Root, -Scale * Root, z - slope.y * Scale * Root, 1.f};
			transforms.push_back(glm::scale(glm::rotate(shear, yaw, {0, 1, 0}), glm::vec3(Scale)));
		}
		return transforms;
	}

	bool Foliage::Load(const std::filesystem::path& path, std::string& error)
	{
		try
		{
			std::ifstream stream(path);
			if (!stream) throw std::runtime_error("打不开植被配置文件");
			const YAML::Node data = YAML::Load(stream);
			Count = data["Count"].as<uint32_t>(Count);
			Seed = data["Seed"].as<uint32_t>(Seed);
			if (YAML::Node size = data["Size"]) Size = {size[0].as<float>(), size[1].as<float>()};
			if (YAML::Node dir = data["Direction"]) Direction = {dir[0].as<float>(), dir[1].as<float>()};
			Scale = data["Scale"].as<float>(Scale);
			Root = data["Root"].as<float>(Root);
			Lean = data["Lean"].as<float>(Lean);
			Frequency = data["Frequency"].as<float>(Frequency);
			Wavelength = data["Wavelength"].as<float>(Wavelength);
			if (Count > 65536 || !(Size.x >= 0 && Size.y >= 0 && Scale > 0 && Wavelength > 0) ||
				!std::isfinite(Size.x + Size.y + Scale + Root + Lean + Frequency + Wavelength + Direction.x + Direction.y))
				throw std::runtime_error("检查植被数量（最多 65536）、范围、缩放和风的波长");
			error.clear();
			return true;
		}
		catch (const std::exception& e)
		{
			error = e.what();
			return false;
		}
	}

	bool Foliage::Save(const std::filesystem::path& path, std::string& error) const
	{
		YAML::Node data;
		data["Count"] = Count;
		data["Seed"] = Seed;
		data["Size"] = std::vector<float>{Size.x, Size.y};
		data["Direction"] = std::vector<float>{Direction.x, Direction.y};
		data["Scale"] = Scale;
		data["Root"] = Root;
		data["Lean"] = Lean;
		data["Frequency"] = Frequency;
		data["Wavelength"] = Wavelength;

		const std::filesystem::path temporary = path.wstring() + L".tmp";
		std::ofstream stream(temporary);
		stream << data << '\n';
		stream.close();
		std::error_code moveError;
		if (stream) std::filesystem::rename(temporary, path, moveError);
		if (!stream || moveError)
		{
			error = "无法保存植被配置文件";
			std::filesystem::remove(temporary, moveError);
			return false;
		}
		error.clear();
		return true;
	}

	void FoliageComponent::Reload()
	{
		m_Path = Profile;
		++Revision;
		m_Data.emplace();
		if (!m_Data->Load(std::filesystem::u8path(Profile), Error)) { m_Data.reset(); m_Placements.clear(); }
	}

	Foliage* FoliageComponent::Get()
	{
		if (Profile != m_Path) Reload();
		return m_Data ? &*m_Data : nullptr;
	}

	const std::vector<glm::mat4>& FoliageComponent::Placements()
	{
		if (!Get()) return m_Placements;
		// 场景颜色、拾取和阴影读取同一帧的植株姿态。
		const double time = Wind ? Seconds : 0;
		if (m_PoseTime != time || m_PoseWind != Wind || m_PoseRevision != Revision)
		{
			m_Placements = m_Data->Place(time, Wind);
			m_PoseTime = time;
			m_PoseWind = Wind;
			m_PoseRevision = Revision;
		}
		return m_Placements;
	}
}
