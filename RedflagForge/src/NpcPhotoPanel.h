#pragma once

#include "NpcCamera.h"
#include "Redflag/AI/PhotoQuestion.h"

namespace Redflag
{
	class NpcPhotoPanel
	{
	public:
		void Draw(VulkanContext& context, const Ref<Scene>& scene, const std::filesystem::path& path, uint64_t actor);
		void Connect(VulkanContext& context, const Ref<Scene>& scene, const std::filesystem::path& path);
		void Update();
		void Stop();

	private:
		Scope<NpcCamera> m_Camera;
		PhotoQuestion m_Question;
		struct Look
		{
			uint64_t Ticket = 0;
			PhotoQuestion Question;
		};
		std::map<uint64_t, Look> m_Looks;
		std::weak_ptr<Scene> m_Scene;
		char m_Text[512] = {};
		int m_Subject = int(PhotoSubject::Description);
		std::filesystem::path m_SavedImage;
		std::string m_Error;
	};
}
