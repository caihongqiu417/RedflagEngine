#pragma once

#include "support/renderdoc_app.h"
#include <Windows.h>
#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

/* 测试专用截帧入口。显式提供 DLL 时才加载，必须早于 GLFW 创建图形上下文。 */
class RenderDocCapture
{
public:
    explicit RenderDocCapture(const char* libraryPath,
        const std::filesystem::path& outputPath = "../output/captures/DepthPrepass",
        const std::string& title = "Depth prepass / main pass: BaseColor alpha x AlphaMap x 0.75",
        const std::string& comments = "32x32 offscreen regression test, not an editor scene. "
            "Depth and main passes are drawn independently and compared at every pixel. "
            "Expected visible columns: 0..7 and 24..31. This capture is not a GPU timing benchmark.")
        : m_Title(title), m_Comments(comments)
    {
        if (!libraryPath)
            return;

        // RenderDoc 安装的图形钩子会持续到进程退出，因此这里不提前卸载 DLL。
        auto library = LoadLibraryW(std::filesystem::u8path(libraryPath).c_str());
        Check(library != nullptr, "Cannot load RenderDoc DLL");
        auto getAPI = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(library, "RENDERDOC_GetAPI"));
        Check(getAPI != nullptr, "RenderDoc DLL does not export GetAPI");
        Check(getAPI(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&m_API)) == 1,
            "RenderDoc API 1.6 is unavailable");

        const auto output = std::filesystem::absolute(outputPath);
        std::filesystem::create_directories(output.parent_path());
        m_API->SetCaptureFilePathTemplate(output.u8string().c_str());
        int major, minor, patch;
        m_API->GetAPIVersion(&major, &minor, &patch);
        std::cout << "RenderDoc DLL: " << libraryPath << "\nCapture API: "
                  << major << '.' << minor << '.' << patch << '\n';
    }

    void Begin()
    {
        if (!m_API)
            return;
        // 测试只有一个窗口和一个 OpenGL 上下文，空句柄选择这个唯一目标。
        m_API->StartFrameCapture(nullptr, nullptr);
        Check(m_API->IsFrameCapturing() == 1, "RenderDoc did not start capturing");
        m_API->SetCaptureTitle(m_Title.c_str());
    }

    void End()
    {
        if (!m_API)
            return;
        Check(m_API->EndFrameCapture(nullptr, nullptr) == 1, "RenderDoc capture failed");
        const uint32_t count = m_API->GetNumCaptures();
        const uint32_t index = count - 1;
        uint32_t length = 0;
        Check(m_API->GetCapture(index, nullptr, &length, nullptr) == 1, "Capture path is unavailable");
        std::vector<char> filename(length + 1);
        Check(m_API->GetCapture(index, filename.data(), &length, nullptr) == 1, "Cannot read capture path");
        m_API->SetCaptureFileComments(filename.data(), m_Comments.c_str());
        std::cout << "RenderDoc captures: " << count << " (EndFrameCapture/GetCapture succeeded)\n"
                  << "RenderDoc capture: " << filename.data() << '\n';
    }

private:
    static void Check(bool result, const char* message)
    {
        if (!result)
        {
            std::cerr << message << " (Windows error " << GetLastError() << ")\n";
            std::exit(1);
        }
    }

    RENDERDOC_API_1_6_0* m_API = nullptr;
    std::string m_Title;
    std::string m_Comments;
};
