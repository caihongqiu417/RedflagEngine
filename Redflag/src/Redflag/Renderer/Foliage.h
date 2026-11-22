#pragma once

#include "Redflag/Core/Core.h"
#include <glm/glm.hpp>
#include <filesystem>
#include <optional>
#include <vector>

namespace Redflag
{
	/* 同一片地上的模型共用一份种植范围和风设置。Size、Root 与 Wavelength 的单位为米。 */
	struct Foliage
	{
		uint32_t Count = 32, Seed = 1;
		glm::vec2 Size{12.f, 12.f}, Direction{1.f, 0.f};
		float Scale = 1.f, Lean = .16f, Frequency = .5f, Wavelength = 6.f, Root = 0.f;

		std::vector<glm::mat4> Place(double seconds, bool windy) const;
		bool Load(const std::filesystem::path& file, std::string& error);
		bool Save(const std::filesystem::path& file, std::string& error) const;
	};

	struct FoliageComponent
	{
		std::string Profile, Error;
		bool Visible = true, Wind = true;
		double Seconds = 0;
		uint32_t Revision = 0;

		Foliage* Get();
		void Reload();
		const std::vector<glm::mat4>& Placements();
	private:
		std::string m_Path;
		std::optional<Foliage> m_Data;
		std::vector<glm::mat4> m_Placements;
		uint32_t m_PoseRevision = 0;
		double m_PoseTime = -1;
		bool m_PoseWind = false;
	};
}
