#include"rfpch.h"
#include"SceneHierachyPanel.h"
#include"imgui.h"
#include"imgui_internal.h"
#include<filesystem>
#include<glm/gtc/type_ptr.hpp>
#include "Scene/Components.h"
#include "Redflag/Core/AssetManager.h"
#include "Redflag/Renderer/Model.h"

namespace Redflag {

	SceneHierachyPanel::SceneHierachyPanel(const Ref<Scene>& context)
	{
		SetContext(context);
	}

	void SceneHierachyPanel::SetContext(const Ref<Scene>& context)
	{
		m_Context = context;
        m_SelectionContext = {};
	}

    void SceneHierachyPanel::OnImguiRender()
    {
        ImGui::Begin("Scene Hierachy");

        if (m_Context) {
            // ��������ʵ�岢���ƽڵ�
            auto view = m_Context->m_Registry.view<TagComponent>();
            view.each([&](auto entity, auto& tc) {
                // ����Entity���󲢻��ƽڵ�
                Entity ent{ entity, m_Context.get() };
                DrawEntityNode(ent);
                });
        }

        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
            m_SelectionContext = {};
        
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
            
            /*
            if (ImGui::Button("Add Component"))
                ImGui::OpenPopup("Add Component");

            if (ImGui::BeginPopup("Add Component"))
            {
                if (ImGui::MenuItem("Camera"))
                {
                    m_SelectionContext.AddComponent<CameraComponent>();
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Sprite Renderer"))
                {
                    m_SelectionContext.AddComponent<SpriteRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
            */
        }

        ImGui::End();
    }

    void SceneHierachyPanel::SetSelectedEntity(Entity entity)
    {
        m_SelectionContext = entity;
    }

    void SceneHierachyPanel::DrawEntityNode(Entity entity)
    {
        auto& tagComponent = entity.GetComponent<TagComponent>();

        // ���ýڵ��־
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
        if (m_SelectionContext == entity)
            flags |= ImGuiTreeNodeFlags_Selected;

        // ���ݱ�־��ʵ����ƽڵ�
        bool opened = ImGui::TreeNodeEx(
            (void*)(uint64_t)(uint32_t)entity,
            flags,
            "%s",
            tagComponent.Tag.c_str()
        );

        // ���ѡ��
        if (ImGui::IsItemClicked()) {
            m_SelectionContext = entity;
        }

        // �ӳ�ɾ�����
        bool entityDeleted = false;

        // �Ҽ��˵� - ʹ�� BeginPopupContextItem ����ȷ��ʽ
        if (ImGui::BeginPopupContextItem())  // ����Ҫ�����жϣ������Զ������Ҽ����
        {
            if (ImGui::MenuItem("Delete Entity"))
                entityDeleted = true;  // ֻ���ñ�ǣ�������ɾ��

            ImGui::EndPopup();
        }

        // ����ڵ�򿪣�����������
        if (opened) {
            //������
            //
            //
            ImGui::TreePop();
        }

        // �ӳ�ɾ�����ڽڵ������ɺ�ִ��
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

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.2f, 0.6f, 1.0f));        // �޺��
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.4f, 0.8f, 1.0f));  // ���޺��
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.5f, 1.0f));   // ���޺��
        if (ImGui::Button("x", buttonSize))
            values.x = resetValue;
        ImGui::PopStyleColor(3);


        ImGui::SameLine();
        ImGui::DragFloat("##x", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();


        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 1.0f, 0.4f, 1.0f));        // �޺���
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 1.0f, 0.6f, 1.0f));  // ���޺���
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.9f, 0.3f, 1.0f));   // ���޺���
        if (ImGui::Button("y", buttonSize))
            values.y = resetValue;
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::DragFloat("##y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::PopItemWidth();
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));        // �����
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));  // �������
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.5f, 0.9f, 1.0f));   // �������
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

    template<typename T, typename UIFunction>
    static void DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction)
    {
        const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;

        if (entity.HasComponent<T>())
        {
            auto& component = entity.GetComponent<T>();
            ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();
            
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4,4 });
            float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.f;
            ImGui::Separator();
            bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), treeNodeFlags, name.c_str());
            ImGui::PopStyleVar();
            ImGui::SameLine(contentRegionAvailable.x-lineHeight*0.5f);
            if (ImGui::Button("+", ImVec2{ lineHeight,lineHeight }))
            {
                ImGui::OpenPopup("ComponentSettings");
            }

            bool removeComponent = false;
            if (ImGui::BeginPopup("ComponentSettings"))
            {
                if (ImGui::MenuItem("Remove component"))
                    removeComponent = true;

                ImGui::EndPopup();
            }

            if (open)
            {
                auto& src = entity.GetComponent<T>();
                uiFunction(component);
                ImGui::TreePop();
            }

            if (removeComponent)
                entity.RemoveComponent<T>();
        }
    }

    void SceneHierachyPanel::DrawComponents(Entity entity)
    {
        if (!entity) return;

       
        const ImGuiTreeNodeFlags tagTreeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
        // Tag��� 
        if (entity.HasComponent<TagComponent>())
        {
            auto& tag = entity.GetComponent<TagComponent>().Tag;
            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strcpy_s(buffer, sizeof(buffer), tag.c_str());
            if (ImGui::InputText("##ReName", buffer, sizeof(buffer)))
            {
                tag = std::string(buffer);
            }
        }

        ImGui::SameLine();
        ImGui::PushItemWidth(-1);
        if (ImGui::Button("Add Comp"))
            ImGui::OpenPopup("Add Comp");

        if (ImGui::BeginPopup("Add Comp"))
        {
            if (!m_SelectionContext.HasComponent<CameraComponent>())
            {
                if (ImGui::MenuItem("Camera"))
                {
                    m_SelectionContext.AddComponent<CameraComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            
            if (!m_SelectionContext.HasComponent<SpriteRendererComponent>())
            {
                if (ImGui::MenuItem("Sprite Renderer"))
                {
                    m_SelectionContext.AddComponent<SpriteRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!m_SelectionContext.HasComponent<CircleRendererComponent>())
            {
                if (ImGui::MenuItem("Circle Renderer"))
                {
                    m_SelectionContext.AddComponent<CircleRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }
            
            if (!m_SelectionContext.HasComponent<RigidBody2DComponent>())
            {
                if (ImGui::MenuItem("Rigidbody 2D"))
                {
                    m_SelectionContext.AddComponent<RigidBody2DComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!m_SelectionContext.HasComponent<BoxCollider2DComponent>())
            {
                if (ImGui::MenuItem("Box Collider 2D"))
                {
                    m_SelectionContext.AddComponent<BoxCollider2DComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!m_SelectionContext.HasComponent<CircleCollider2DComponent>())
            {
                if (ImGui::MenuItem("Circle Collider 2D"))
                {
                    m_SelectionContext.AddComponent<CircleCollider2DComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!m_SelectionContext.HasComponent<MeshRendererComponent>())
            {
                if (ImGui::MenuItem("Mesh Renderer"))
                {
                    auto& mr = m_SelectionContext.AddComponent<MeshRendererComponent>();
                    mr.MeshData = Mesh::CreateCube();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!m_SelectionContext.HasComponent<ModelRendererComponent>())
            {
                if (ImGui::MenuItem("Model Renderer"))
                {
                    m_SelectionContext.AddComponent<ModelRendererComponent>();
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }
        ImGui::PopItemWidth();


        // Transform���
        DrawComponent<TransformComponent>("Transform", entity, [](auto& component)
            {
                DrawVec3Control("Translation", component.Translation);
                glm::vec3 rotation = glm::degrees(component.Rotation);
                DrawVec3Control("Rotation", rotation);
                component.Rotation = glm::radians(rotation);
                DrawVec3Control("Scale", component.Scale, 1.0f);
            });

        // Camera���
        DrawComponent<CameraComponent>("Camera", entity, [](auto& component)
            {
                auto& camera = component.Camera;

                ImGui::Checkbox("Primary", &component.Primary);

                const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
                const char* currentProjectionTypeString = projectionTypeStrings[(int)camera.GetProjectionType()];

                if (ImGui::BeginCombo("CamType", currentProjectionTypeString))
                {
                    for (int i = 0; i < 2; i++)
                    {
                        bool isSelected = (currentProjectionTypeString == projectionTypeStrings[i]);
                        if (ImGui::Selectable(projectionTypeStrings[i], isSelected))
                        {
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

                    ImGui::Checkbox("Fixed AspectRatio", &component.FixedAspectRatio);
                }

            });

        // SpriteRender��� 
        DrawComponent<SpriteRendererComponent>("Sprite Renderer", entity, [](auto& component)
            {
                // ��ɫ�༭
                ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));

                // ƽ������
                ImGui::DragFloat("Tiling Factor", &component.TilingFactor, 0.1f, 0.0f, 100.f);

                // ========== ������ק���� ==========
                ImGui::Separator();
                ImGui::Text("Texture");

                // ��ʾ��ǰ����Ԥ��
                if (component.Texture && component.Texture->GetRendererID())
                {
                    // ����Ԥ��
                    ImGui::Image(
                        (void*)(intptr_t)component.Texture->GetRendererID(),
                        ImVec2(64, 64)
                    );
                }
                else
                {
                    // ������ռλ
                    ImGui::Button("No Texture", ImVec2(64, 64));
                }

                // ��קĿ��
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_TEXTURE"))
                    {
                        const char* path = (const char*)payload->Data;
                        if (path)
                        {
                            component.Texture = Texture2D::Create(path);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // ���������ť
                if (component.Texture)
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear Texture"))
                    {
                        component.Texture = nullptr;
                    }
                }
                // =================================
            });


        //CircleRendererComponent
        DrawComponent<CircleRendererComponent>("Circle Renderer", entity, [](auto& component)
            {
                ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));
                ImGui::DragFloat("Thickness", &component.Thickness, 0.025f, 0.f, 1.f);
                ImGui::DragFloat("Fade", &component.Fade, 0.0025f, 2.f, 1.f);
            });

        // Rigidbody2D ���
        DrawComponent<RigidBody2DComponent>("Rigidbody 2D", entity, [](auto& component)
            {
                // ��������������
                const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
                const char* currentType = bodyTypeStrings[(int)component.Type];

                if (ImGui::BeginCombo("Body Type", currentType))
                {
                    for (int i = 0; i < 3; i++)
                    {
                        bool isSelected = (currentType == bodyTypeStrings[i]);
                        if (ImGui::Selectable(bodyTypeStrings[i], isSelected))
                        {
                            component.Type = (RigidBody2DComponent::BodyType)i;
                        }
                        if (isSelected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                // �̶���ת
                ImGui::Checkbox("Fixed Rotation", &component.FixedRotation);
            });

        // BoxCollider2D ���
        DrawComponent<BoxCollider2DComponent>("Box Collider 2D", entity, [](auto& component)
            {
                // ƫ����
                ImGui::DragFloat2("Offset", glm::value_ptr(component.Offset));
                // ��ײ�гߴ�
                ImGui::DragFloat2("Size", glm::value_ptr(component.Size));
                // �ܶ�
                ImGui::DragFloat("Density", &component.Density, 0.01f, 0.0f, 1.0f);
                // Ħ����
                ImGui::DragFloat("Friction", &component.Friction, 0.01f, 0.0f, 1.0f);
                // ����
                ImGui::DragFloat("Restitution", &component.Restitution, 0.01f, 0.0f, 1.0f);
                // ������ֵ
                ImGui::DragFloat("Restitution Threshold", &component.RestitutionThreshold, 0.01f, 0.0f);
            });

        // CircleCollider2D 属性面板
        DrawComponent<CircleCollider2DComponent>("Circle Collider 2D", entity, [](auto& component)
            {
                // 碰撞圆心相对实体原点的偏移
                ImGui::DragFloat2("Offset", glm::value_ptr(component.Offset));
                // 碰撞圆的半径
                ImGui::DragFloat("Radius", &component.Radius, 0.01f, 0.001f, FLT_MAX);
                // 密度
                ImGui::DragFloat("Density", &component.Density, 0.01f, 0.0f, 1.0f);
                // 摩擦系数
                ImGui::DragFloat("Friction", &component.Friction, 0.01f, 0.0f, 1.0f);
                // 恢复系数（弹性）
                ImGui::DragFloat("Restitution", &component.Restitution, 0.01f, 0.0f, 1.0f);
                // 恢复生效的最低碰撞速度阈值
                ImGui::DragFloat("Restitution Threshold", &component.RestitutionThreshold, 0.01f, 0.0f);
            });

        // Mesh Renderer
        DrawComponent<MeshRendererComponent>("Mesh Renderer", entity, [](auto& component)
            {
                // 颜色
                ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));

                // 纹理
                ImGui::Separator();
                ImGui::Text("Texture");

                if (component.Texture && component.Texture->GetRendererID())
                {
                    ImGui::Image(
                        (void*)(intptr_t)component.Texture->GetRendererID(),
                        ImVec2(64, 64)
                    );
                }
                else
                {
                    ImGui::Button("No Texture", ImVec2(64, 64));
                }

                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_TEXTURE"))
                    {
                        const char* path = (const char*)payload->Data;
                        if (path)
                            component.Texture = Texture2D::Create(path);
                    }
                    ImGui::EndDragDropTarget();
                }

                if (component.Texture)
                {
                    ImGui::SameLine();
                    if (ImGui::Button("Clear Texture"))
                        component.Texture = nullptr;
                }
            });

        // Model Renderer
        DrawComponent<ModelRendererComponent>("Model Renderer", entity, [](auto& component)
            {
                // 文件路径（只读显示）
                ImGui::Text("File: %s", component.FilePath.empty() ? "(none)" : component.FilePath.c_str());

                // 拖拽接受区：从 Content Browser 拖入模型文件
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_MODEL"))
                    {
                        const char* path = (const char*)payload->Data;
                        if (path)
                        {
                            component.FilePath    = path;
                            component.ModelAsset  = AssetManager::GetOrLoad<Model>(component.FilePath);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // 颜色（无贴图时使用）
                ImGui::ColorEdit4("Color", glm::value_ptr(component.Color));

                // 卸载按钮
                if (component.ModelAsset)
                {
                    if (ImGui::Button("Unload"))
                    {
                        component.ModelAsset.reset();
                    }
                }
            });
    }
}