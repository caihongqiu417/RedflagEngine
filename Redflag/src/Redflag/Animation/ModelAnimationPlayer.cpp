#include "rfpch.h"
#include "ModelAnimationPlayer.h"
#include "Redflag/Renderer/Model.h"

namespace Redflag
{
	void ModelAnimationComponent::Update(const Ref<Model>& model, float seconds)
	{
		if (model != m_Model) {
			m_Model = model;
			m_Animation = model ? model->GetAnimation() : nullptr;
			m_Meshes.clear();
			m_Transforms.clear();
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
		if (Playing && seconds > 0.f && active->Duration > 0.f) Time = std::fmod(Time + seconds, active->Duration);
		if (Time == m_LastTime && Clip == m_LastClip) return;
		m_LastTime = Time;
		m_LastClip = Clip;

		const std::vector<ModelAnimation::Part> pose = m_Animation->Sample(active - clips.begin(), Time);
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
