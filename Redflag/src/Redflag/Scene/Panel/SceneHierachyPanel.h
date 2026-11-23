#pragma once

#include"Redflag/Core/Core.h"
#include "FieldInspector.h"
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

		void OpenFieldNotes(const std::filesystem::path& scene) { m_Fields.Open(scene); }
		void SaveFieldNotesAs(const std::filesystem::path& scene) { m_Fields.SaveAs(scene); }

		void OnImguiRender(const char* hierarchy = "Scene Hierachy", const char* inspector = "Properties");

		Entity GetSelectedEntity()const { return m_SelectionContext; }


		void SetSelectedEntity(Entity entity);
	private:
		void DrawEntityNode(Entity entity);
		void DrawComponents(Entity entity);
	private:
		FieldInspector m_Fields;
		Ref<Scene> m_Context;
		Entity m_SelectionContext;
		std::string m_NewItem;
		Entity m_EntityToDelete;
	};
}
