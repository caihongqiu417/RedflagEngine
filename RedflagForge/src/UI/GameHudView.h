#pragma once

#include "Redflag/Gameplay/GameHud.h"
#include "GameConversation.h"
#include "Redflag/Renderer/Texture.h"
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <imgui.h>
#include <map>

namespace Redflag
{
	class Scene;
	class VulkanDraw;

	/* 场景指定界面布局。位置、字体大小和图片来自布局文件，屏幕文字来自当前一局的脚本。 */
	class GameHudView
	{
	public:
		const ConversationStyle& Draw(Scene& scene, VulkanDraw& device, const glm::mat4& viewProjection, ImVec2 corner, ImVec2 size);

	private:
		struct Sprite
		{
			Ref<Texture2D> Image;
			ImTextureID ID{};
		};

		struct Element
		{
			std::string Text, Meter, When;
			Sprite Image;
			ImVec2 Anchor{}, Position{}, Size{};
			float Font = 18;
			ImU32 Color = IM_COL32_WHITE;
			bool DuringPlay = false;
		};

		struct Layout
		{
			std::vector<Element> Elements;
			Sprite Plate, Bar;
			ConversationStyle Chat;
			ImU32 PlateColor = IM_COL32_WHITE;
			ImVec2 ShadowOffset{};
			ImU32 ShadowColor = 0;
			float NameSize = 18;
		};

		static Layout Load(VulkanDraw& device, const std::filesystem::path& path);
		std::map<std::string, Layout> m_Layouts;
	};
}
