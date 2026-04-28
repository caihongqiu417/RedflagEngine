#pragma once

#include"Redflag/Core/Core.h"
#include"Redflag/Scene/Scene.h"
#include"Redflag/Scene/Entity.h"
#include"Redflag/Scripting/ScriptEngine.h"


namespace Redflag {

	class SceneHierachyPanel
	{
	public:
		SceneHierachyPanel() = default;
		SceneHierachyPanel(const Ref<Scene>& context);

		void SetContext(const Ref<Scene>& context);

		void OnImguiRender();

		Entity GetSelectedEntity()const { return m_SelectionContext; }


		void SetSelectedEntity(Entity entity);
	private:
		void DrawEntityNode(Entity entity);
		void DrawComponents(Entity entity);
	private:
		Ref<Scene> m_Context;
		Entity m_SelectionContext;
	};
}