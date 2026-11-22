#include "rfpch.h"
#include "EnvironmentSense.h"
#include "Redflag/Scene/Entity.h"

namespace Redflag
{
	// ---- 区域范围与关联灯具 ----
	static bool Contains(const TransformComponent& transform, glm::vec3 halfSize, glm::vec3 position)
	{
		// 将角色的位置转回区域的三个轴，再和每个方向的半长相比。负缩放只会调换两侧。
		glm::vec3 local = glm::inverse(glm::quat(transform.Rotation)) * (position - transform.Translation);
		glm::vec3 extent = glm::abs(transform.Scale) * halfSize;
		return glm::all(glm::greaterThan(extent, glm::vec3(0))) && glm::all(glm::lessThanEqual(glm::abs(local), extent));
	}

	static std::string ReadLight(Entity lamp, glm::vec3 observer)
	{
		std::ostringstream text;
		glm::vec3 color;
		float intensity;
		if (lamp.HasComponent<PointLightComponent>())
		{
			const PointLightComponent& light = lamp.GetComponent<PointLightComponent>();
			color = light.Color;
			intensity = light.Intensity;
			const float distance = glm::distance(observer, lamp.GetComponent<TransformComponent>().Translation);
			text << "关联点光源「" << lamp.GetName() << "」，距角色 " << distance << " 米，影响半径 " << light.Radius << " 米";
		}
		else if (lamp.HasComponent<DirectionalLightComponent>())
		{
			const DirectionalLightComponent& light = lamp.GetComponent<DirectionalLightComponent>();
			color = light.Color;
			intensity = light.Intensity;
			text << "关联方向光「" << lamp.GetName() << "」";
		}
		else return {};

		const bool on = intensity > 0.f && glm::any(glm::greaterThan(color, glm::vec3(0)));
		text << (on ? "，已开启" : "，无光输出") << "，强度 " << intensity;
		text << "，RGB (" << color.r << ", " << color.g << ", " << color.b << ")";
		return text.str();
	}

	std::vector<DecisionFact> SenseEnvironment(Scene& scene, uint64_t actor)
	{
		std::vector<DecisionFact> facts;
		Entity observer = scene.GetEntityByUUID(actor);
		if (!observer) return facts;
		const glm::vec3 position = observer.GetComponent<TransformComponent>().Translation;
		auto zones = scene.GetRegistry().view<EnvironmentZoneComponent, TransformComponent>();
		for (entt::entity handle : zones)
		{
			const EnvironmentZoneComponent& zone = zones.get<EnvironmentZoneComponent>(handle);
			if (!Contains(zones.get<TransformComponent>(handle), zone.HalfSize, position)) continue;
			Entity area(handle, &scene);
			const std::string key = "zone/" + std::to_string(area.GetUUID()) + "/";
			facts.push_back({key + "name", "角色所在区域：" + area.GetName()});
			if (!zone.Landform.empty()) facts.push_back({key + "landform", "作者标注的地貌：" + zone.Landform});
			if (!zone.Vegetation.empty()) facts.push_back({key + "vegetation", "作者标注的植被：" + zone.Vegetation});
			if (!zone.Landmark.empty()) facts.push_back({key + "landmark", "作者标注的地标：" + zone.Landmark});

			for (uint64_t id : zone.Lights)
			{
				Entity lamp = scene.GetEntityByUUID(id);
				if (!lamp) continue;
				std::string description = ReadLight(lamp, position);
				if (!description.empty()) facts.push_back({key + "light/" + std::to_string(id), std::move(description)});
			}
		}
		std::sort(facts.begin(), facts.end(), [](const DecisionFact& a, const DecisionFact& b) { return a.ID < b.ID; });
		return facts;
	}
}
