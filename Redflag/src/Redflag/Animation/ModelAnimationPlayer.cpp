#include "rfpch.h"
#include "ModelAnimationPlayer.h"
#include "Redflag/Renderer/Model.h"

namespace Redflag
{
	bool ModelAnimationComponent::GetNodeTransform(const std::string& name, glm::mat4& localToModel) const
	{
		return m_Animation && m_Animation->GetNodeTransform(name, m_NodeTransforms, localToModel);
	}

	void ModelAnimationComponent::Update(const Ref<Model>& model, float seconds, const AdjustPose& adjust)
	{
		if (model != m_Model) {
			m_Model = model;
			m_Animation = model ? model->GetAnimation() : nullptr;
			m_Meshes.clear();
			m_Transforms.clear();
			m_NodeTransforms.clear();
			m_Local.clear();
			m_Previous.clear();
			m_LastTime = -1.f;
		}
		if (!m_Animation) return;

		const std::vector<ModelAnimation::Clip>& clips = m_Animation->GetClips();
		auto active = std::find_if(clips.begin(), clips.end(), [&](const ModelAnimation::Clip& item) { return item.Name == Clip; });
		if (active == clips.end()) {
			active = clips.begin();
			Clip = active->Name;
		}
		Time = glm::clamp(Time, 0.f, active->Duration);
		// 深度、颜色和拾取可能重复读取同一姿势。读取时保留上一步的过渡进度。
		if (seconds == 0.f && Time == m_LastTime && Clip == m_LastClip) return;
		if (Playing && seconds > 0.f && active->Duration > 0.f)
			Time = Loop ? std::fmod(Time + seconds, active->Duration) : std::min(Time + seconds, active->Duration);
		if (!Playing || seconds <= 0.f || BlendSeconds <= 0.f)
			m_Previous.clear();
		else if (Clip != m_LastClip)
		{
			// 中途又改动作时，从屏幕上刚刚看到的姿势开始，手和脚都会接着动。
			m_Previous = m_Local;
			m_BlendTime = 0.f;
		}
		if (Time == m_LastTime && Clip == m_LastClip && m_Previous.empty()) return;
		m_LastTime = Time;
		m_LastClip = Clip;

		m_Animation->SamplePose(active - clips.begin(), Time, m_Local);
		if (!m_Previous.empty())
		{
			m_BlendTime += seconds;
			if (m_BlendTime < BlendSeconds)
				ModelAnimation::BlendPose(m_Previous, m_BlendTime / BlendSeconds, m_Local);
			else
				m_Previous.clear();
		}

		// 蒙皮前以本帧姿势贴合地面或平台。原动作保留在 m_Local，下次切换片段时仍从原动作混合。
		std::vector<ModelAnimation::NodePose> adjusted;
		if (adjust) {
			adjusted = m_Local;
			adjust(*m_Animation, adjusted);
		}
		const std::vector<ModelAnimation::Part> pose = m_Animation->BuildParts(adjust ? adjusted : m_Local, m_NodeTransforms);
		const auto& parts = model->GetSubMeshes();
		if (pose.size() != parts.size()) return;
		m_Meshes.resize(parts.size());
		m_Transforms.resize(parts.size());
		for (size_t i = 0; i < parts.size(); ++i) {
			m_Transforms[i] = pose[i].Transform;
			m_Meshes[i] = pose[i].Vertices.empty() ? parts[i].MeshData : Mesh::Create(pose[i].Vertices, m_Animation->GetIndices(i));
		}
	}
}
