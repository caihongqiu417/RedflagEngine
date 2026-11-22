#pragma once

#include <cstdint>
#include <future>
#include <optional>
#include <string>
#include <glm/vec3.hpp>

namespace Redflag
{
	class Scene;

	/* 从照片的拍摄位置朝标注框中心看。只有方向，没有目标距离。 */
	struct NpcViewRegion
	{
		std::string Camera, ID, Label;
		glm::vec3 Origin, Direction;
	};

	struct NpcVisionCandidate
	{
		std::optional<uint64_t> Entity;
		std::optional<NpcViewRegion> Region;
	};

	std::future<NpcVisionCandidate> AskNpcCandidate(std::string sceneName, std::string question, uint16_t port = 8715);

	// 在工作线程等待本机视觉服务。返回空值表示在这组图片里没有选出物体。
	std::future<std::optional<uint64_t>> AskNpcVision(std::string sceneName, std::string question, uint16_t port = 8715);

	// 收到候选后，在场景线程用当前碰撞体复查。reach 是从手的位置量起的距离，单位为米。
	bool CanPickUp(Scene& scene, uint64_t candidate, glm::vec3 hand, float reach);
}
