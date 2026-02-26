#include"rfpch.h"
#include"SceneHierachyPanel.h"
#include"imgui.h"
#include "Scene/Components.h"

namespace Redflag {

	SceneHierachyPanel::SceneHierachyPanel(const Ref<Scene>& context)
	{
		SetContext(context);
	}

	void SceneHierachyPanel::SetContext(const Ref<Scene>& context)
	{
		m_Context = context;
	}

	void SceneHierachyPanel::OnImguiRender()
	{
		ImGui::Begin("Scene Hierachy");

		if (m_Context) {
			// 遍历所有实体并绘制节点
			auto view = m_Context->m_Registry.view<TagComponent>();
			view.each([&](auto entity, auto& tc) {
				// 创建Entity对象并绘制节点
				Entity ent{ entity, m_Context.get() };
				DrawEntityNode(ent);
				});
		}

		ImGui::End();
	}

    void SceneHierachyPanel::DrawEntityNode(Entity entity)
    {
        auto& tagComponent = entity.GetComponent<TagComponent>();

        // 设置节点标志
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (m_SelectionContext == entity)
            flags |= ImGuiTreeNodeFlags_Selected;

        // 根据标志和实体绘制节点
        bool opened = ImGui::TreeNodeEx(
            (void*)(uint64_t)(uint32_t)entity,
            flags,
            "%s",
            tagComponent.Tag.c_str()
        );

        // 点击选择，下一帧修改标志
        if (ImGui::IsItemClicked()) {
            m_SelectionContext = entity;
        }

        // 如果节点打开，可以添加子内容
        if (opened) {
            // 这里可以添加实体的组件列表
            //ImGui::Text("Transform Component");
            // ImGui::Text("Camera Component");
            ImGui::TreePop();
        }
    }
}