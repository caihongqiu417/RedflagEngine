#include "CharacterModel.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

Redflag::SkinnedMesh MakeCharacterModel(const Redflag::Skeleton& rig)
{
	using namespace Redflag;
	Pose rest = rig.ToModelSpace(rig.RestPose());
	std::vector<MeshVertex> vertices;
	std::vector<SkinWeights> influences;
	std::vector<uint32_t> indices;
	auto limb = [&](int parent, int child, glm::vec3 start, glm::vec3 end, float width, float depth, float color) {
		glm::vec3 direction = glm::normalize(end - start);
		glm::vec3 side = glm::normalize(glm::cross(direction,
			std::abs(direction.z) < .85f ? glm::vec3(0,0,1) : glm::vec3(0,1,0)));
		glm::vec3 front = glm::cross(side, direction);
		constexpr int segments = 12;
		uint32_t base = static_cast<uint32_t>(vertices.size());
		for (int ring = 0; ring <= 6; ++ring)
		{
			float t = float(ring) / 6.0f;
			float radius = std::sqrt(std::max(0.0f, 1.0f - (2*t-1)*(2*t-1)));
			for (int s = 0; s < segments; ++s)
			{
				float angle = glm::two_pi<float>() * s / segments;
				glm::vec3 radial = side * std::cos(angle) + front * std::sin(angle);
				glm::vec3 normal = glm::normalize(radial * radius + direction * (2*t-1));
				glm::vec3 p = glm::mix(start,end,t) + (2*t-1)*direction*.018f +
					radius * (side * (width*std::cos(angle)) + front * (depth*std::sin(angle)));
				float shade = glm::clamp(.55f + .4f * glm::dot(normal,glm::normalize(glm::vec3(-1,2,2))),.1f,.95f);
				vertices.push_back({p,normal,{shade,color},side});
				float blend = parent == child ? 0.0f : t*t*t * .5f;
				influences.push_back({{static_cast<unsigned>(parent),static_cast<unsigned>(child)},{1-blend,blend}});
				if (ring == 0) continue;
				uint32_t a = base+(ring-1)*segments+s, b = base+(ring-1)*segments+(s+1)%segments;
				indices.insert(indices.end(),{a,b,a+segments,b,b+segments,a+segments});
			}
		}
	};
	for (size_t i = 1; i < rig.Joints.size(); ++i)
	{
		const Joint& joint = rig.Joints[i];
		bool torso = joint.Name == "torso_head" || joint.Name == "lowerback_torso" || joint.Name == "pelvis_lowerback";
		bool leg = joint.Name.find("Knee") != std::string::npos || joint.Name.find("Ankle") != std::string::npos;
		float width = torso ? .15f : leg ? .067f : .043f;
		limb(joint.Parent, static_cast<int>(i),rest[joint.Parent].Position,rest[i].Position,
			width,torso ? .09f : width,leg ? .25f : .75f);
	}
	int head = rig.Find("torso_head");
	limb(head,head,rest[head].Position,rest[head].Position+glm::vec3(0,.23f,0),.088f,.093f,.75f);
	for (const char* name : {"lWrist","rWrist"})
	{
		int hand = rig.Find(name);
		float length = name[0] == 'l' ? .105f : -.105f;
		limb(hand,hand,rest[hand].Position,rest[hand].Position+glm::vec3(length,0,0),.038f,.026f,.75f);
	}
	return SkinnedMesh(rig,std::move(vertices),std::move(influences),std::move(indices));
}
