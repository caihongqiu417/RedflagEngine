#include "rfpch.h"
#include "ContentBrowserPanel.h"

#include<imgui.h>
#include<filesystem>

namespace Redflag
{
	// 资源根目录路径
	static std::filesystem::path m_AssetPath = "assets";

	ContentBrowserPanel::ContentBrowserPanel()
		: m_CurrentDirectory(m_AssetPath)  // 初始化当前目录为根目录
	{
	}

	void ContentBrowserPanel::OnImGuiRender()
	{
		ImGui::Begin("Content Browser");

		// 返回上一级目录（不在根目录时显示）
		if (m_CurrentDirectory != std::filesystem::path(m_AssetPath))
		{
			if (ImGui::Button("<-"))
				m_CurrentDirectory = m_CurrentDirectory.parent_path();
		}

		// 遍历当前目录下的所有条目
		for (auto& directoryEntry : std::filesystem::directory_iterator(m_CurrentDirectory))
		{
			// 获取相对路径并提取文件名
			auto relativePath = std::filesystem::relative(directoryEntry.path(), m_AssetPath);
			std::string filenamestring = relativePath.filename().string();

			if (directoryEntry.is_directory())
			{
				// 目录：点击进入子目录
				if (ImGui::Button(filenamestring.c_str()))
				{
					m_CurrentDirectory = m_CurrentDirectory / directoryEntry.path().filename();
				}
			}
			else
			{
				// 文件：点击选中（TODO: 实现文件打开逻辑）
				if (ImGui::Button(filenamestring.c_str()))
				{
					// 预留：打开文件
				}
			}
		}

		ImGui::End();
	}
}