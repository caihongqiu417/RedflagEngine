#pragma once

#include "ModelAnimation.h"

namespace Redflag
{
	class Model;

	/* 模型拥有动作文件。每个实体单独保存播放进度和当前画面的网格。 */
	struct ModelAnimationComponent
	{
		std::string Clip;
		float Time = 0.f; // 秒，可在暂停时拖动。
		bool Playing = true;

		void Update(const Ref<Model>& model, float seconds);
		const Ref<ModelAnimation>& GetAnimation() const { return m_Animation; }
		const std::vector<Ref<Mesh>>& GetMeshes() const { return m_Meshes; }
		const glm::mat4& GetTransform(size_t part) const { return m_Transforms[part]; }

	private:
		Ref<Model> m_Model;
		Ref<ModelAnimation> m_Animation;
		std::vector<Ref<Mesh>> m_Meshes;
		std::vector<glm::mat4> m_Transforms;
		std::string m_LastClip;
		float m_LastTime = -1.f;
	};
}
