#include "rfpch.h"
#include "ModelAnimationPlayer.h"
#include "Redflag/Renderer/Model.h"

namespace Redflag
{
	void ModelAnimationComponent::PlayClip(const std::string& clip, const AnimationController* controller,
		const AnimationState* state, bool restart)
	{
		Playing = true;
		if (!state && controller) state = controller->FindClip(clip);
		const std::string next = state ? state->ID : "";
		if (!restart && clip == Clip && next == m_Transition.To) return;

		// 同一帧改令两次时，第一次请求的动作还没画出来，过渡仍从上次显示的姿势开始。
		std::string previous = m_ChangePending ? m_Transition.From : m_Transition.To;
		if (previous.empty() && controller)
			if (const AnimationState* from = controller->FindClip(Clip)) previous = from->ID;
		const AnimationTransition* edge = controller ? controller->FindTransition(previous, next) : nullptr;
		m_Transition = edge ? *edge : AnimationTransition{previous, next, state ? state->Blend : BlendSeconds};
		m_ChangePending = true;
		Clip = clip;
		Time = 0.f;
	}

	bool ModelAnimationComponent::GetNodeTransform(const std::string& name, glm::mat4& localToModel) const
	{
		return m_Animation && m_Animation->GetNodeTransform(name, m_NodeTransforms, localToModel);
	}

	void ModelAnimationComponent::Update(const Ref<Model>& model, float seconds, const AdjustPose& adjust, const GaitStep* gait, bool poseChanged)
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
			m_StrideRoot.clear();
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
		if (!m_ChangePending && !poseChanged && seconds == 0.f && Time == m_LastTime && Clip == m_LastClip) return;
		if (Playing && seconds > 0.f && active->Duration > 0.f)
		{
			float advance = seconds;
			if (MatchTravel && gait && !gait->Root.empty())
			{
				if (Clip != m_LastClip || gait->Root != m_StrideRoot) {
					m_Stride = m_Animation->GetStrideLength(active - clips.begin(), gait->Root);
					m_StrideRoot = gait->Root;
				}
				// 走过半个步态周期的距离，就播放半个周期。原地制作、没有位移曲线的片段沿用原速。
				if (m_Stride > .0001f) advance = active->Duration * gait->Distance / m_Stride;
			}
			else m_StrideRoot.clear();
			Time = Loop ? std::fmod(Time + advance, active->Duration) : std::min(Time + advance, active->Duration);
		}
		if (!m_ChangePending && Clip != m_LastClip)
			m_Transition = AnimationTransition{{}, {}, BlendSeconds};
		if (!Playing || m_Transition.Seconds <= 0.f)
			m_Previous.clear();
		else if (m_ChangePending || Clip != m_LastClip)
		{
			// 中途又改动作时，从屏幕上刚刚看到的姿势开始，手和脚都会接着动。
			m_Previous = m_Local;
			m_BlendTime = 0.f;
		}
		const bool changed = m_ChangePending;
		m_ChangePending = false;
		if (!changed && Time == m_LastTime && Clip == m_LastClip && m_Previous.empty() && !poseChanged) return;
		m_LastTime = Time;
		m_LastClip = Clip;

		m_Animation->SamplePose(active - clips.begin(), Time, m_Local);
		if (!m_Previous.empty())
		{
			m_BlendTime += seconds;
			if (m_BlendTime < m_Transition.Seconds)
				ModelAnimation::BlendPose(m_Previous, m_Transition.Weight(m_BlendTime / m_Transition.Seconds), m_Local);
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
