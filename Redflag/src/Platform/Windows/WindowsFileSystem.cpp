#include "rfpch.h"
#include "Redflag/Utils/FileSystem.h"
#include <Windows.h>
#include <atomic>
#include <fstream>

namespace Redflag::FileSystem
{
	static std::filesystem::path CreateTemporaryFile(const std::filesystem::path& directory)
	{
		static std::atomic_uint64_t sequence{ 0 };
		for (;;)
		{
			const std::filesystem::path path = directory / ("Redflag-" + std::to_string(GetCurrentProcessId()) +
				"-" + std::to_string(sequence++) + ".tmp");
			HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file != INVALID_HANDLE_VALUE)
			{
				CloseHandle(file);
				return path;
			}
			const DWORD error = GetLastError();
			if (error != ERROR_FILE_EXISTS)
				throw std::filesystem::filesystem_error("Cannot create temporary file", path,
					std::error_code(error, std::system_category()));
		}
	}

	void Write(const std::filesystem::path& path, std::ios::openmode mode,
		const std::function<void(std::ostream&)>& write)
	{
		const std::filesystem::path temporary = CreateTemporaryFile(path.parent_path());
		try
		{
			std::ofstream file(temporary, mode);
			file.exceptions(std::ios::failbit | std::ios::badbit);
			write(file);
			file.close();
			std::filesystem::rename(temporary, path);
		}
		catch (...)
		{
			std::error_code error;
			std::filesystem::remove(temporary, error);
			throw;
		}
	}

	void WriteText(const std::filesystem::path& path, std::string_view text)
	{
		Write(path, std::ios::out, [&](std::ostream& out) {
			out.write(text.data(), static_cast<std::streamsize>(text.size()));
		});
	}
}
