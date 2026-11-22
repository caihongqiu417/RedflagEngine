#include "rfpch.h"
#include "NpcCamera.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/Renderer/SceneRenderer.h"
#include "Redflag/Scene/Entity.h"
#include <glm/gtc/matrix_transform.hpp>
#include "../../Redflag/vendor/stb_image/stb_image_write.h"

namespace Redflag
{
	NpcCamera::NpcCamera(VulkanContext& context)
		: m_Objects(context), m_Lighting(context, m_Objects.GetSampler())
	{
		m_Lighting.Load({});
	}

	static DecisionImage EncodePng(std::string name, const std::vector<glm::u8vec4>& pixels, glm::ivec2 size)
	{
		DecisionImage image{std::move(name), "image/png"};
		const auto write = [](void* context, void* data, int bytes) {
			auto& png = *static_cast<std::vector<uint8_t>*>(context);
			const auto* begin = static_cast<const uint8_t*>(data);
			png.insert(png.end(),begin,begin+bytes);
		};
		if (!stbi_write_png_to_func(write,&image.Bytes,size.x,size.y,4,pixels.data(),size.x*4))
			throw std::runtime_error("Cannot encode scene camera PNG");
		return image;
	}

	NpcPhoto NpcCamera::Take(Scene& scene, uint64_t observer, const std::filesystem::path& scenePath, bool includeObjects)
	{
		NpcPhoto photo;
		NpcSystem* npcs = scene.GetNpcs();
		const NpcDefinition* npc = npcs ? npcs->GetDefinition(observer) : nullptr;
		if (!npc || !npc->Memory.Pictures) return photo;
		if (m_Path != scenePath) { m_Lighting.Load(scenePath); m_Path = scenePath; }
		m_Lighting.Mode = Shading::Lit;

		const Entity actor = scene.GetEntityByUUID(observer);
		const TransformComponent& transform = actor.GetComponent<TransformComponent>();
		const glm::quat rotation(transform.Rotation);
		photo.Observer = observer;
		photo.Seconds = npcs->SessionSeconds();
		photo.Size = {448, 252}; // 给模型的 16:9 小图只在提问时绘制，避开界面文字。
		photo.Forward = rotation * glm::vec3(0,0,1);
		const glm::vec3 up = rotation * glm::vec3(0,1,0);
		photo.Eye = NpcEyePosition(transform,*npc);
		const glm::mat4 view = glm::lookAt(photo.Eye, photo.Eye + photo.Forward, up);
		const glm::mat4 projection = glm::perspective(glm::radians(npc->VerticalFov), float(photo.Size.x)/photo.Size.y, .05f, 500.f);

		m_Objects.Resize(photo.Size.x, photo.Size.y);
		m_Objects.DrawSurfaces([&](CommandList& list) { SceneRenderer::DrawGeometry(scene, list, projection * view); });
		std::vector<glm::u8vec4> pixels(size_t(photo.Size.x)*photo.Size.y);
		std::vector<int> entities(pixels.size());
		m_Objects.ReadRegion(0, 0, photo.Size.x, photo.Size.y, pixels.data(), entities.data());
		if (m_Lighting.Render(scene, projection, view, photo.Eye, photo.Size.x, photo.Size.y, false, false))
		{
			std::vector<glm::u8vec4> lit = m_Lighting.ReadDisplayPixels();
			if (m_Lighting.NeedsModelOverlay())
				for (size_t i=0; i<lit.size(); ++i)
					if (entities[i] != -1) lit[i] = pixels[i];
			pixels = std::move(lit);
		}

		photo.Image = EncodePng(std::to_string(observer)+"-"+std::to_string(uint64_t(UUID{})),pixels,photo.Size);
		if (!includeObjects) return photo;

		// 大物体的外框可能包住旁边的小物体。特写中只保留真正画在该物体上的像素。
		struct Box { glm::ivec4 Rect; int Count; };
		std::map<int,Box> boxes;
		for (int y=0; y<photo.Size.y; ++y)
			for (int x=0; x<photo.Size.x; ++x)
			{
				const int id = entities[size_t(y)*photo.Size.x+x];
				if (id == -1) continue;
				Box& object = boxes.try_emplace(id, Box{{x,y,x,y},0}).first->second;
				++object.Count;
				glm::ivec4& box = object.Rect;
				box = {std::min(box.x,x),std::min(box.y,y),std::max(box.z,x),std::max(box.w,y)};
			}
		std::vector<int> sorted;
		for (const auto& [id, box] : boxes)
		{
			Entity item(static_cast<entt::entity>(id), &scene);
			if (item.GetUUID() != observer) sorted.push_back(id);
		}
		std::stable_sort(sorted.begin(),sorted.end(),[&](int a,int b) { return boxes[a].Count > boxes[b].Count; });
		// 每次最多看八张特写，优先选占画面较大的物体，以免本地模型被几十张图占满上下文。
		for (size_t i=0; i<std::min<size_t>(8,sorted.size()); ++i)
		{
			const int id = sorted[i];
			const glm::ivec4 rect = boxes[id].Rect;
			const glm::ivec2 original(rect.z-rect.x+1, rect.w-rect.y+1);
			const float scale = std::min(1.f, 128.f / std::max(original.x,original.y));
			const glm::ivec2 size = glm::max(glm::ivec2(1),glm::ivec2(glm::round(glm::vec2(original)*scale)));
			std::vector<glm::u8vec4> cut(size_t(size.x)*size.y,glm::u8vec4(55,55,55,255));
			for (int y=0; y<size.y; ++y)
				for (int x=0; x<size.x; ++x)
				{
					const size_t source = size_t(rect.y + y*original.y/size.y)*photo.Size.x + rect.x + x*original.x/size.x;
					if (entities[source] == id) cut[size_t(y)*size.x+x] = pixels[source];
				}
			Entity item(static_cast<entt::entity>(id), &scene);
			photo.Regions.push_back({item.GetUUID(),rect,EncodePng(std::string(1,char('A'+i)),cut,size)});
		}
		return photo;
	}
}
