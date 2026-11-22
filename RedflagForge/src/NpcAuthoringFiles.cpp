#include "rfpch.h"
#include "NpcAuthoringFiles.h"
#include "ForgeTool.h"
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

namespace Redflag
{
    namespace fs = std::filesystem;

    fs::path ChooseNpcTable(bool save)
    {
        wchar_t path[MAX_PATH]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = GetActiveWindow();
        dialog.lpstrFile = path;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrFilter = L"TSV / CSV\0*.tsv;*.csv\0";
        dialog.lpstrDefExt = L"tsv";
        dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        const BOOL selected = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
        return selected ? fs::path(path) : fs::path{};
    }

    fs::path NpcTemporaryFile()
    {
        wchar_t path[MAX_PATH];
        if (!GetTempFileNameW(fs::temp_directory_path().c_str(), L"rfn", 0, path))
            throw std::runtime_error("无法创建角色工具的临时文件。");
        return path;
    }

    YAML::Node RunForgeTool(const char* scriptName, const YAML::Node& request)
    {
        fs::path temporary;
        YAML::Node result;
        try
        {
            // 从编辑器安装目录向上找仓库。切换场景、打开文件对话框不会改变工具的位置。
            wchar_t executable[MAX_PATH];
            GetModuleFileNameW(nullptr, executable, MAX_PATH);
            fs::path root = fs::path(executable).parent_path();
            while (root != root.root_path() && !fs::is_regular_file(root / "scripts/redflag_mcp" / scriptName))
                root = root.parent_path();
            fs::path python = root / "bin-int/redflag-mcp/Scripts/python.exe";
            fs::path script = root / "scripts/redflag_mcp" / scriptName;
            if (!fs::is_regular_file(python) || !fs::is_regular_file(script))
                throw std::runtime_error("请按 scripts/redflag_mcp/README.md 准备 Forge 工具的 Python 环境。");

            temporary = NpcTemporaryFile();
            {
                std::ofstream out(temporary);
                out.exceptions(std::ios::badbit | std::ios::failbit);
                out << YAML::Dump(request);
            }
            // 命令行只传程序和文件路径。角色说的话、音色和表格内容都从文件读。
            std::wstring command =
                L"\"" + python.wstring() + L"\" -X utf8 \"" + script.wstring() + L"\" \"" + temporary.wstring() + L"\"";
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(python.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                root.c_str(), &startup, &process))
                throw std::runtime_error("启动 Forge 工具失败。");
            CloseHandle(process.hThread);
            WaitForSingleObject(process.hProcess, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(process.hProcess, &code);
            CloseHandle(process.hProcess);
            if (code != 0)
                throw std::runtime_error("Forge 工具运行失败，请检查本地 Python 环境。");
            std::ifstream reply(temporary);
            result = YAML::Load(reply);
        }
        catch (const std::exception& e)
        {
            result["error"] = e.what();
        }

        if (!temporary.empty())
        {
            std::error_code error;
            fs::remove(temporary, error);
        }
        return result;
    }

    YAML::Node RunNpcAuthoringTool(const YAML::Node& request)
    {
        return RunForgeTool("forge_authoring.py", request);
    }
}
