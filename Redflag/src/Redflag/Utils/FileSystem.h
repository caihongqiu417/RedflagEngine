#pragma once

#include <filesystem>
#include <functional>
#include <ostream>
#include <string_view>

namespace Redflag::FileSystem
{
	// 在同一目录写完新文件再替换原件，写到一半出错时，读者仍能打开上次保存的版本。
	void Write(const std::filesystem::path& path, std::ios::openmode mode,
		const std::function<void(std::ostream&)>& write);

	void WriteText(const std::filesystem::path& path, std::string_view text);
}
