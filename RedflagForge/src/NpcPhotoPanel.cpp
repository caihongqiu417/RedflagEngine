#include "rfpch.h"
#include "NpcPhotoPanel.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/Scene/Scene.h"
#include <imgui.h>

namespace Redflag
{
	void NpcPhotoPanel::Draw(VulkanContext& context, const Ref<Scene>& scene, const std::filesystem::path& path, uint64_t actor)
	{
		if (!ImGui::CollapsingHeader("从队友眼中观察")) return;
		const NpcDefinition* npc = scene->GetNpcs()->GetDefinition(actor);
		if (!npc || !npc->Memory.Pictures)
		{
			ImGui::TextDisabled("该角色的 ObservePictures 已关闭。");
			return;
		}
		const char* subjects[] = {"找画面中的物体", "辨别明暗", "描述所见"};
		ImGui::Combo("观察内容", &m_Subject, subjects, IM_ARRAYSIZE(subjects));
		ImGui::InputTextWithHint("##photo-question", "例如：面前的道路亮吗？", m_Text, sizeof(m_Text));
		ImGui::BeginDisabled(m_Question.Pending() || !m_Text[0]);
		if (ImGui::Button("拍照并提问"))
		{
			m_Error.clear();
			m_SavedImage.clear();
			try
			{
				if (!m_Camera) m_Camera = CreateScope<NpcCamera>(context);
				NpcPhoto photo = m_Camera->Take(*scene, actor, path, m_Subject == int(PhotoSubject::Object));
				if (!m_Question.Start(scene, std::move(photo), static_cast<PhotoSubject>(m_Subject), m_Text))
					m_Error = "当前没有可用的角色照片。";
			}
			catch (const std::exception& error) { m_Error = error.what(); }
		}
		ImGui::EndDisabled();
		const NpcPhoto& image = m_Question.GetPhoto();
		if (image.Observer == actor && !image.Image.Bytes.empty())
		{
			ImGui::TextDisabled("拍摄于本局 %.2f 秒 · %d × %d", image.Seconds, image.Size.x, image.Size.y);
			if (ImGui::Button("保存这张原图"))
			{
				const auto folder = std::filesystem::path("captures/npc");
				std::filesystem::create_directories(folder);
				m_SavedImage = folder / (image.Image.ID + ".png");
				std::ofstream file(m_SavedImage, std::ios::binary);
				file.write(reinterpret_cast<const char*>(image.Image.Bytes.data()), image.Image.Bytes.size());
				if (!file) { m_Error = "照片写入失败"; m_SavedImage.clear(); }
			}
			if (!m_SavedImage.empty()) ImGui::TextWrapped("%s",m_SavedImage.u8string().c_str());
			if (m_Question.Pending()) ImGui::TextUnformatted("正在查看照片…");
			else ImGui::TextWrapped("%s  %s",m_Question.Answer().c_str(),m_Question.Receipt().c_str());
		}
		if (!m_Error.empty()) ImGui::TextWrapped("%s",m_Error.c_str());
	}

	void NpcPhotoPanel::Stop()
	{
		m_Question.Clear();
		m_Camera.reset();
		m_Error.clear();
		m_SavedImage.clear();
	}
}
