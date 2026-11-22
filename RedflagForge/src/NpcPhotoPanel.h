#pragma once

#include "NpcCamera.h"
#include "Redflag/AI/PhotoQuestion.h"

namespace Redflag
{
	class NpcPhotoPanel
	{
	public:
		void Draw(VulkanContext& context, const Ref<Scene>& scene, const std::filesystem::path& path, uint64_t actor);
		void Update() { m_Question.Update(); }
		void Stop();

	private:
		Scope<NpcCamera> m_Camera;
		PhotoQuestion m_Question;
		char m_Text[512] = {};
		int m_Subject = 0;
		std::filesystem::path m_SavedImage;
		std::string m_Error;
	};
}
