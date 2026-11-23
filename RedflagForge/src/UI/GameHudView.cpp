#include "rfpch.h"
#include "GameHudView.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Scene/Scene.h"
#include <yaml-cpp/yaml.h>

namespace Redflag
{
	static ImVec2 Pair(const YAML::Node& n) { return {n[0].as<float>(),n[1].as<float>()}; }

	static ImU32 ReadColor(const YAML::Node& n)
	{
		return IM_COL32(n[0].as<int>(),n[1].as<int>(),n[2].as<int>(),n[3].as<int>());
	}

	GameHudView::Layout GameHudView::Load(VulkanDraw& device, const std::filesystem::path& path)
	{
		std::ifstream input(path);
		const YAML::Node doc = YAML::Load(input);
		std::map<std::string, Sprite> images;
		for (const auto& entry : doc["Sprites"]) {
			const auto file = path.parent_path() / entry.second.as<std::string>();
			if (!AssetManager::GetLibrary().Add(file, AssetType::Texture))
				throw std::runtime_error("Missing HUD sprite: " + file.string());
			auto texture = Texture2D::Create(file.string());
			if (!texture->IsLoaded()) throw std::runtime_error("Cannot open HUD sprite: " + file.string());
			images[entry.first.as<std::string>()] = {texture,(ImTextureID)device.GetPreviewTexture(texture->GetSampledTexture())};
		}
		Layout layout;
		if (const YAML::Node shadow = doc["TextShadow"])
		{
			layout.ShadowOffset = Pair(shadow["Offset"]);
			layout.ShadowColor = ReadColor(shadow["Color"]);
		}

		for (const auto& item : doc["Elements"]) {
			Element e;
			e.Anchor = Pair(item["Anchor"]);
			e.Position = Pair(item["Position"]);
			if (item["Text"]) {
				e.Text = item["Text"].as<std::string>();
				e.Font = item["Font"].as<float>();
			} else {
				e.Image = images.at(item["Sprite"].as<std::string>());
				e.Size = Pair(item["Size"]);
				e.Meter = item["Meter"].as<std::string>("");
			}
			if (item["Color"]) e.Color = ReadColor(item["Color"]);
			e.When = item["When"].as<std::string>("");
			e.DuringPlay = item["DuringPlay"].as<bool>(false);
			layout.Elements.push_back(e);
		}
		if (const YAML::Node labels = doc["Nameplates"])
		{
			if (labels["Background"])
				layout.Plate = images.at(labels["Background"].as<std::string>());
			layout.Bar = images.at(labels["HealthBar"].as<std::string>());
			if (labels["Color"])
				layout.PlateColor = ReadColor(labels["Color"]);
			layout.NameSize = labels["Font"].as<float>();
		}
		if (const YAML::Node chat = doc["Conversation"])
		{
			layout.Chat.Subtitles = chat["Subtitles"].as<bool>(true);
			if (chat["Speaker"]) layout.Chat.Speaker = ReadColor(chat["Speaker"]);
			if (chat["Text"]) layout.Chat.Text = ReadColor(chat["Text"]);
			layout.Chat.Font = chat["Font"].as<float>(layout.Chat.Font);
			layout.Chat.BottomMargin = chat["BottomMargin"].as<float>(layout.Chat.BottomMargin);
			layout.Chat.ReadingRate = chat["ReadingRate"].as<float>(layout.Chat.ReadingRate);
		}

		return layout;
	}

	const ConversationStyle& GameHudView::Draw(Scene& scene, VulkanDraw& device, const glm::mat4& vp, ImVec2 corner, ImVec2 size)
	{
		static const ConversationStyle defaultChat;
		auto messages = scene.GetRegistry().view<HudComponent>();
		if (messages.begin() == messages.end()) return defaultChat;
		const entt::entity owner = *messages.begin();
		static const GameHud defaultHud;
		const GameHud* live = scene.GetRegistry().try_get<GameHud>(owner);
		const GameHud& hud = live ? *live : defaultHud;
		const std::string& path = messages.get<HudComponent>(owner).Layout;
		if (path.empty()) return defaultChat;
		auto [it,fresh] = m_Layouts.try_emplace(path);
		if (fresh) {
			try { it->second = Load(device,std::filesystem::u8path(path)); }
			catch (const std::exception& error) { RF_CORE_ERROR("HUD: {0} ({1})",error.what(),path); }
		}
		const Layout& layout = it->second;
		if (layout.Elements.empty()) return layout.Chat;

		ImDrawList* draw = ImGui::GetWindowDrawList();
		const float scale = std::min(size.x/1920.f,size.y/1080.f);
		draw->PushClipRect(corner, {corner.x+size.x,corner.y+size.y},true);
		auto image = [&](const Sprite& sprite, ImVec2 min, ImVec2 max, ImU32 tint) {
			// 材质加载器把图片第一行放在底边。面板直接显示 PNG，翻回它在文件中的上下方向。
			draw->AddImage(sprite.ID,min,max,{0,1},{1,0},tint);
		};
		auto caption = [&](ImVec2 position, float fontSize, ImU32 color, const std::string& text) {
			if (layout.ShadowColor & IM_COL32_A_MASK)
			{
				ImVec2 behind{position.x + scale * layout.ShadowOffset.x, position.y + scale * layout.ShadowOffset.y};
				draw->AddText(ImGui::GetFont(), fontSize, behind, layout.ShadowColor, text.c_str());
			}
			draw->AddText(ImGui::GetFont(), fontSize, position, color, text.c_str());
		};

		// ---- 布局中的文字、图片和进度条 ----
		for (const Element& element : layout.Elements)
		{
			if (element.DuringPlay && hud.Finished) continue;
			if (!element.When.empty())
			{
				const auto condition = hud.Text.find(element.When);
				if (condition == hud.Text.end() || condition->second.empty()) continue;
			}
			ImVec2 position{corner.x + size.x * element.Anchor.x + scale * element.Position.x,
				corner.y + size.y * element.Anchor.y + scale * element.Position.y};

			if (!element.Text.empty())
			{
				const auto value = hud.Text.find(element.Text);
				if (value != hud.Text.end() && !value->second.empty())
					caption(position, element.Font * scale, element.Color, value->second);
				continue;
			}

			float width = element.Size.x * scale;
			if (!element.Meter.empty())
			{
				const auto fraction = hud.Meters.find(element.Meter);
				if (fraction == hud.Meters.end() || fraction->second <= 0.f) continue;
				width *= fraction->second;
			}
			image(element.Image, position, {position.x + width, position.y + scale * element.Size.y}, element.Color);
		}

		if (!layout.Bar.Image) { draw->PopClipRect(); return layout.Chat; }

		scene.GetRegistry().view<PulseTrace>().each([&](const PulseTrace& shot) {
			if (shot.Until < std::chrono::steady_clock::now()) return;
			glm::vec4 a=vp*glm::vec4(shot.From,1), b=vp*glm::vec4(shot.To,1);
			if (a.w<=0 || b.w<=0) return;
			glm::vec2 start{corner.x+(.5f+.5f*a.x/a.w)*size.x,corner.y+(.5f-.5f*a.y/a.w)*size.y};
			glm::vec2 end{corner.x+(.5f+.5f*b.x/b.w)*size.x,corner.y+(.5f-.5f*b.y/b.w)*size.y};
			glm::vec2 direction=end-start;
			if (glm::length(direction)<1) return;
			glm::vec2 side=glm::normalize(glm::vec2{-direction.y,direction.x})*2.f*scale;
			draw->AddImageQuad(layout.Bar.ID,{start.x-side.x,start.y-side.y},{end.x-side.x,end.y-side.y},
				{end.x+side.x,end.y+side.y},{start.x+side.x,start.y+side.y},{0,1},{1,1},{1,0},{0,0},IM_COL32(141,239,255,242));
		});

		// 角色名字投影到游戏镜头中，生命条随角色当前生命值缩短。
		scene.GetRegistry().view<Nameplate,TagComponent,HealthComponent,TransformComponent>().each(
			[&](const Nameplate& label, const TagComponent& tag, const HealthComponent& health, const TransformComponent& pose) {
				glm::vec4 clip = vp*glm::vec4(pose.Translation+glm::vec3(0,label.Height,0),1);
				if (clip.w <= 0 || std::abs(clip.z) > clip.w) return;
				float x=corner.x+(.5f+.5f*clip.x/clip.w)*size.x, y=corner.y+(.5f-.5f*clip.y/clip.w)*size.y;
				// 给贴着边缘的长名字留出可见部分，整个标牌在视口外才跳过绘制。
				float font=layout.NameSize*scale;
				if (y < corner.y - 4*scale || y - font - 8*scale > corner.y + size.y) return;
				float half=ImGui::GetFont()->CalcTextSizeA(font,FLT_MAX,0,tag.Tag.c_str()).x*.5f+7.f*scale;
				if (x + half < corner.x || x - half > corner.x + size.x) return;
				if (layout.Plate.Image)
					image(layout.Plate,{x-half,y-font-8*scale},{x+half,y+4*scale},layout.PlateColor);
				caption({x-half+7*scale,y-font-4*scale},font,IM_COL32_WHITE,tag.Tag);
				float life=health.Maximum>0 ? std::clamp(health.Current/health.Maximum,0.f,1.f) : 0.f;
				if (life>0) image(layout.Bar,{x-half,y},{x-half+2*half*life,y+3*scale},
					ImGui::ColorConvertFloat4ToU32({label.Color.r,label.Color.g,label.Color.b,1}));
			});
		draw->PopClipRect();
		return layout.Chat;
	}
}
