#include"rfpch.h"
#include"SceneHierachyPanel.h"
#include"imgui.h"
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

        ImGui::End();

        ImGui::Begin("Properties");
        // 如果有选中的实体，显示其属性
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

        // 点击选择，下一帧修改标志
        if (ImGui::IsItemClicked()) {
            m_SelectionContext = entity;
        }

        // 如果节点打开，可以添加子内容
        if (opened) {
            // 这里可以添加子内容
            //ImGui::Text("Transform Component");
            // ImGui::Text("Camera Component");
            ImGui::TreePop();
        }
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

        /*
        [ 1.0   0.0   0.0   px ]
        [ 0.0   1.0   0.0   py ]
        [ 0.0   0.0   1.0   pz ]
        [ 0.0   0.0   0.0   1.0]  ← 第四列：位置
        */
        // Transform组件
        if (entity.HasComponent<TransformComponent>())
        {
            if (ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto& transform = entity.GetComponent<TransformComponent>();
                glm::vec3 position(transform.Transform[3]);
                if (ImGui::DragFloat3("Position", &position.x, 0.1f))
                {
                    transform.Transform[3] = glm::vec4(position, 1.0f);
                }
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