#pragma once

#include "SkinnedMesh.h"
#include <filesystem>
#include <memory>

namespace Redflag
{
	/* 模型里的节点、逆绑定矩阵和动作可以让多个角色共用。Sample 只计算这一次姿势，不改变资源。 */
	class ModelAnimation
	{
	public:
		struct Clip { std::string Name; float Duration; };
		struct Part { glm::mat4 Transform{1.f}; std::vector<MeshVertex> Vertices; };

		static Ref<ModelAnimation> Load(const std::filesystem::path& path);
		~ModelAnimation();
		const std::vector<Clip>& GetClips() const { return m_Clips; }
		// 蒙皮部件的顶点会更新，三角形编号始终相同。刚性部件直接画原模型，这里返回空数组。
		const std::vector<uint32_t>& GetIndices(size_t part) const;
		std::vector<Part> Sample(size_t clip, float seconds) const;

	private:
		struct Data;
		ModelAnimation();
		std::unique_ptr<Data> m_Data;
		std::vector<Clip> m_Clips;
	};
}
