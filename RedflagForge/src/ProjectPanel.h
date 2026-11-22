#pragma once

#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace Redflag
{
	/* 双击文件夹浏览项目，双击场景或模型把路径交给编辑器。 */
	class ProjectPanel
	{
	public:
		std::filesystem::path Draw();

	private:
		void OpenDirectory(const std::filesystem::path& path);

		std::filesystem::path m_Directory;
		std::vector<std::filesystem::directory_entry> m_Entries;
		std::future<std::string> m_SurfaceBuild;
		std::string m_BuildMessage;
		char m_Filter[128]{};
	};
}
