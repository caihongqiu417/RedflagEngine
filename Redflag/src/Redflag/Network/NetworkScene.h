#pragma once

#include "WorldClient.h"
#include "entt.hpp"
#include <unordered_map>

namespace Redflag
{
	class Scene;

	// 主机在物理步之后读取场景。列表中的实体若已删除，下一份快照里就不再出现它。
	std::vector<WorldPose> CapturePoses(Scene& scene, const std::vector<uint64_t>& ids);

	/* 保留远端 UUID，供游戏给对应实体加模型。新建的展示实体只带 Transform，不参与本地物理模拟。 */
	class NetworkScene
	{
	public:
		explicit NetworkScene(Scene& scene) : m_Scene(scene) {}
		~NetworkScene();

		NetworkScene(const NetworkScene&) = delete;
		NetworkScene& operator=(const NetworkScene&) = delete;

		// 遇到同 UUID 的本地实体返回 false，整份快照都不写入。
		bool Apply(const std::vector<WorldPose>& poses);

	private:
		Scene& m_Scene;
		// UUID 在网络上传递；本地句柄用来区分游戏后来用同一 UUID 恢复的新对象。
		std::unordered_map<uint64_t, entt::entity> m_Replicas;
	};
}
