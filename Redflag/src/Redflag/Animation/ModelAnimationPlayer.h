#pragma once

#include "ModelAnimation.h"
#include <functional>

namespace Redflag
{
	class Model;

	/* 模型拥有动作文件。每个实体单独保存播放进度和当前画面的网格。 */
	struct ModelAnimationComponent
	{
		std::string Clip;
		float Time = 0.f; // 秒，可在暂停时拖动。
		bool Playing = true;
		bool Loop = true;
		float BlendSeconds = .18f; // 切换动作时用于衔接前后姿势的时间，单位为秒。设为零就直接换姿势。

		using AdjustPose = std::function<void(const ModelAnimation&, std::vector<ModelAnimation::NodePose>&)>;
		void Update(const Ref<Model>& model, float seconds, const AdjustPose& adjust = {});
		const Ref<ModelAnimation>& GetAnimation() const { return m_Animation; }
		const std::vector<Ref<Mesh>>& GetMeshes() const { return m_Meshes; }
		const glm::mat4& GetTransform(size_t part) const { return m_Transforms[part]; }
		/// <summary>取得当前屏幕姿势中指定节点相对模型的变换。把物品放到手上时再乘角色的世界变换。</summary>
		bool GetNodeTransform(const std::string& name, glm::mat4& localToModel) const;

	private:
		Ref<Model> m_Model;
		Ref<ModelAnimation> m_Animation;
		std::vector<Ref<Mesh>> m_Meshes;
		std::vector<glm::mat4> m_Transforms;
		std::vector<glm::mat4> m_NodeTransforms;
		std::vector<ModelAnimation::NodePose> m_Local, m_Previous;
		float m_BlendTime = 0.0f;
		std::string m_LastClip;
		float m_LastTime = -1.f;
	};
}
