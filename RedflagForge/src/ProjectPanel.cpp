#include "rfpch.h"
#include "ProjectPanel.h"
#include <imgui.h>

namespace Redflag
{
	enum class AssetKind { Folder, Scene, Model, Other };

	static AssetKind GetAssetKind(const std::filesystem::directory_entry& entry)
	{
		if (entry.is_directory()) return AssetKind::Folder;
		std::string ext = entry.path().extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (ext == ".redflag") return AssetKind::Scene;
		if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".ply") return AssetKind::Model;
		return AssetKind::Other;
	}

	void ProjectPanel::OpenDirectory(const std::filesystem::path& path)
	{
		m_Directory = path;
		m_Entries.clear();
		for (const auto& entry : std::filesystem::directory_iterator(path))
			if (GetAssetKind(entry) != AssetKind::Other)
				m_Entries.push_back(entry);

		std::sort(m_Entries.begin(), m_Entries.end(), [](const auto& a, const auto& b) {
			if (a.is_directory() != b.is_directory()) return a.is_directory();
			return a.path().filename() < b.path().filename();
		});
	}

	std::filesystem::path ProjectPanel::Draw()
	{
		std::filesystem::path chosen;
		if (ImGui::Begin("项目  Project"))
		{
			if (m_Directory.empty())
				OpenDirectory("assets/scenes");
			ImGui::BeginDisabled(m_Directory == "assets");
			if (ImGui::Button("上一级"))
				OpenDirectory(m_Directory.parent_path());
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextUnformatted(m_Directory.generic_u8string().c_str());
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
			ImGui::InputTextWithHint("##filter", "搜索文件", m_Filter, sizeof(m_Filter));
			ImGui::SameLine();
			if (ImGui::SmallButton("刷新")) OpenDirectory(m_Directory);

			std::filesystem::path next;
			if (ImGui::BeginTable("Files", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
			{
				ImGui::TableSetupColumn("名称", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 90);
				ImGui::TableHeadersRow();
				for (const auto& entry : m_Entries)
				{
					auto kind = GetAssetKind(entry);
					std::string name = entry.path().filename().u8string();
					if (name.find(m_Filter) == std::string::npos) continue;
					ImGui::TableNextColumn();
					ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
					if (kind == AssetKind::Model && ImGui::BeginDragDropSource())
					{
						auto path = entry.path().generic_u8string();
						ImGui::SetDragDropPayload("CONTENT_BROWSER_MODEL", path.c_str(), path.size() + 1);
						ImGui::TextUnformatted(name.c_str());
						ImGui::EndDragDropSource();
					}
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
					{
						if (entry.is_directory()) next = entry.path();
						else chosen = entry.path();
					}
					ImGui::TableNextColumn();
					ImGui::TextDisabled("%s", kind == AssetKind::Folder ? "文件夹" : kind == AssetKind::Scene ? "场景" : "模型");
				}
				ImGui::EndTable();
			}
			if (!next.empty()) OpenDirectory(next);
		}
		ImGui::End();
		return chosen;
	}
}
