#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Redflag
{
	class Entity;

	/* 控件读取组件的实际值。随场景保存的取值记录只用来解释当时的设计。 */
	class FieldInspector
	{
	public:
		void Open(const std::filesystem::path& scene);
		void SaveAs(const std::filesystem::path& scene);
		void Draw(Entity entity, const char* component) const;
		void DrawNote(uint64_t entity, const std::string& component, const std::string& field,
			const std::string& script = {}) const;

	private:
		struct Note
		{
			std::vector<uint64_t> Entities;
			std::string Component, Field, Reason, Script;
			std::vector<std::string> Evidence;
		};

		std::filesystem::path m_Path;
		std::vector<Note> m_Notes;
	};
}
