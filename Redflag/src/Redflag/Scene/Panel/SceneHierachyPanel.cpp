#include"rfpch.h"
#include"SceneHierachyPanel.h"
#include"imgui.h"
#include"imgui_internal.h"
#include<glm/gtc/type_ptr.hpp>
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

        // 点击空白区域取消选中：鼠标左键点击窗口空白处时，清空选中
        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
            m_SelectionContext = {};

        // 右击空白区域,添加实体 - 只在空白区域触发
        if (ImGui::BeginPopupContextWindow("HierarchyPopup", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Create Empty Entity"))
                m_Context->CreateEntity("Empty Entity");
            ImGui::EndPopup();
        }

        ImGui::End();

        ImGui::Begin("Properties");
        if (m_SelectionContext && m_Context)
        {
            DrawComponents(m_SelectionContext);
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

        // 点击选择
        if (ImGui::IsItemClicked()) {
            m_SelectionContext = entity;
        }

        // 延迟删除标记
        bool entityDeleted = false;

        // 右键菜单 - 使用 BeginPopupContextItem 的正确方式
        if (ImGui::BeginPopupContextItem())  // 不需要条件判断，它会自动处理右键点击
        {
            if (ImGui::MenuItem("Delete Entity"))
                entityDeleted = true;  // 只设置标记，不立即删除

            ImGui::EndPopup();
        }

        // 如果节点打开，添加子内容
        if (opened) {
            ImGui::TreePop();
        }

        // 延迟删除：在节点绘制完成后执行
        if (entityDeleted)
        {
            m_Context->DestroyEntity(entity);
            if (m_SelectionContext == entity)
                m_SelectionContext = {};
        }
    }

    static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.f)
    {
        ImGui::PushID(label.c_str());

        ImGui::Columns(2);
        ImGui::SetColumnWidth(0, columnWidth);
        ImGui::Text(label.c_str());
        ImGui::NextColumn();

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0,0 });

        float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
        ImVec2 buttonSize = { lineHeight + 3.0f,lineHeight };

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.2f, 0.6f, 1.0f));        // 霓虹粉
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.4f, 0.8f, 1.0f));  // 亮霓虹粉
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.5f, 1.0f));   // 暗霓虹粉
        if (ImGui::Button("x", buttonSize))
            values.x = resetValue;
        ImGui::PopStyleColor(3);


        ImGui::SameLine();
        ImGui::DragFloat("##x", &values.x, 0.1f,0.0f,0.0f,"%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();


        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 1.0f, 0.4f, 1.0f));        // 霓虹绿
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 1.0f, 0.6f, 1.0f));  // 亮霓虹绿
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.9f, 0.3f, 1.0f));   // 暗霓虹绿
        if (ImGui::Button("y", buttonSize))
            values.y = resetValue;
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));        // 电光蓝
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));  // 亮电光蓝
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.5f, 0.9f, 1.0f));   // 暗电光蓝
        if (ImGui::Button("z", buttonSize))
            values.z = resetValue;
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PopStyleVar();

        ImGui::Columns(1);

        ImGui::PopID();
    }

    void SceneHierachyPanel::DrawComponents(Entity entity)
    {
        if (!entity) return;

        // Tag组件 
        if (entity.HasComponent<TagComponent>())
        {
            if (ImGui::TreeNodeEx("Tag", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto& tag = entity.GetComponent<TagComponent>().Tag;
                char buffer[256];
                memset(buffer, 0, sizeof(buffer));
                strcpy_s(buffer, sizeof(buffer), tag.c_str());
                if (ImGui::InputText("ReName", buffer, sizeof(buffer)))
                {
                    tag = std::string(buffer);
                }
                ImGui::TreePop();
            }
        }


        // Transform组件
        if (entity.HasComponent<TransformComponent>())
        {
            if (ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto& transformComponent = entity.GetComponent<TransformComponent>();

                DrawVec3Control("Translation", transformComponent.Translation);
                
                //显示转成角度，用户输入转成弧度
                glm::vec3 rotation = glm::degrees(transformComponent.Rotation);
                DrawVec3Control("Rotation", rotation);
                transformComponent.Rotation = glm::radians(rotation);

                DrawVec3Control("Scale", transformComponent.Scale, 1.0f);

                ImGui::TreePop();
            }
        }

        //Camera组件
        if (entity.HasComponent<CameraComponent>())
        {
            if (ImGui::TreeNodeEx((void*)typeid(CameraComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Camera"))
            {
                auto& cameraComponent = entity.GetComponent<CameraComponent>();
                auto& camera = cameraComponent.Camera;

                ImGui::Checkbox("Primary", &cameraComponent.Primary);
                

                const char* projectionTypeStrings[] = { "Perspective","Orthographic" };
                const char* currentProjectionTypeString = projectionTypeStrings[(int)camera.GetProjectionType()];
                if (ImGui::BeginCombo("ProjectionType", currentProjectionTypeString))
                {
                    for (int i = 0; i < 2; i++)
                    {
                        //是否高亮
                        bool isSelected = (currentProjectionTypeString == projectionTypeStrings[i]);
                        if (ImGui::Selectable(projectionTypeStrings[i], isSelected))
                        {
                            //如果用户点击了->setprojection
                            currentProjectionTypeString = projectionTypeStrings[i];
                            camera.SetProjectionType((SceneCamera::ProjectionType)i);
                        }

                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
                {
                    float verticalFov = glm::degrees(camera.GetPerspectiveFOV());
                    if (ImGui::DragFloat("Fov", &verticalFov))
                        camera.SetPerspectiveFOV(glm::radians(verticalFov));

                    float perspectiveNear = camera.GetPerspectiveNearClip();  
                    if (ImGui::DragFloat("Near", &perspectiveNear))
                        camera.SetPerspectiveNearClip(perspectiveNear);

                    float perspectiveFar = camera.GetPerspectiveFarClip();    
                    if (ImGui::DragFloat("Far", &perspectiveFar))
                        camera.SetPerspectiveFarClip(perspectiveFar);
                }

                if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
                {
                    float orthoSize = camera.GetOrthographicSize();
                    if (ImGui::DragFloat("Size", &orthoSize))
                        camera.SetOrthographicSize(orthoSize);
                    
                    float orthoNear = camera.GetOrthographicNearClip();
                    if (ImGui::DragFloat("Near", &orthoNear))
                        camera.SetOrthographicNearClip(orthoNear);

                    float orthoFar = camera.GetOrthographicFarClip();
                    if (ImGui::DragFloat("Far", &orthoFar))
                        camera.SetOrthographicFarClip(orthoFar);

                    ImGui::Checkbox("Fixed AspectRatio", &cameraComponent.FixedAspectRatio);
                }

                ImGui::TreePop();
            }
        }


        // SpriteRender组件 
        if (entity.HasComponent<SpriteRendererComponent>())
        {
            if (ImGui::TreeNodeEx("Color", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto& spriteRendererComponent = entity.GetComponent<SpriteRendererComponent>();
                ImGui::ColorEdit4("RGBA", glm::value_ptr(spriteRendererComponent.Color));
                ImGui::TreePop();
            }
        }
    }
}