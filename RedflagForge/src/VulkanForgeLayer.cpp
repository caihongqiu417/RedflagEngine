#include "rfpch.h"
#include "VulkanForgeLayer.h"
#include "Redflag/AI/ActorBehavior.h"
#include "CharacterInput.h"
#include "EditorEnvironment.h"

#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Core/Application.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Renderer/SceneRenderer.h"
#include "Redflag/Scene/SceneSerializer.h"
#include "Redflag/Utils/PlatformUtils.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include "Redflag/Math/Math.h"
#include <glm/gtc/type_ptr.hpp>

namespace Redflag
{
	static VulkanContext& GetWindowContext()
	{
		return static_cast<VulkanContext&>(Application::Get().GetWindow().GetGraphicsContext());
	}

	// ---- 场景与资源 ----
	void VulkanForgeLayer::OnAttach()
	{
		m_Draw = CreateScope<VulkanDraw>(GetWindowContext());
		RenderDevice::SetCurrent(m_Draw.get());
		m_Lighting = CreateScope<LightingPreview>(GetWindowContext(), m_Draw->GetSampler());
		NewScene();
	}

	void VulkanForgeLayer::OpenDefaultScene()
	{
		OpenScene("assets/scenes/Sponza-Atrium.redflag");
		if (!std::filesystem::exists("assets/models/reference/Sponza/Sponza.gltf"))
			Report("Sponza model is missing. From the repository, run python tests/fetch_sponza.py and reopen the scene.");
	}

	void VulkanForgeLayer::OnDetach()
	{
		if (m_EditScene) StopScene();
		GetWindowContext().WaitIdle();
		m_Hud = {};
		m_Reviews = {};
		if (m_PanelTexture)
			ImGui_ImplVulkan_RemoveTexture(m_PanelTexture);
		m_Lighting.reset();

		// 场景、属性面板和资源缓存都可能留着网格。先释放它们对网格的引用，销毁网格时还能调用 Vulkan 设备。
		m_Hierarchy.SetContext(nullptr);
		m_Scene.reset();
		m_EditScene.reset();
		AssetManager::ClearAll();
		RenderDevice::SetCurrent(nullptr);
		m_Draw.reset();
	}

	void VulkanForgeLayer::Report(const std::string& message)
	{
		if (m_Messages.size() == 100) m_Messages.pop_front();
		m_Messages.push_back(message);
		RF_CORE_INFO("Editor: {0}", message);
	}

	void VulkanForgeLayer::AddCamera()
	{
		bool primary = !m_Scene->GetPrimaryCameraEntity();
		Entity camera = m_Scene->CreateEntity(primary ? "主相机" : "相机");
		auto& settings = camera.AddComponent<CameraComponent>();
		settings.Primary = primary;
		settings.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
		camera.GetComponent<TransformComponent>().Translation.z = 6;
		m_Hierarchy.SetSelectedEntity(camera);
	}

	void VulkanForgeLayer::NewScene()
	{
		if (m_EditScene) StopScene();
		m_Scene = CreateRef<Scene>();
		m_Scene->SetEnvironment(DefaultSky);
		auto extent = m_Draw->GetExtent();
		if (!extent.width) extent = GetWindowContext().GetExtent();
		const glm::uvec2 gameSize = m_GameViewport.Resolution({extent.width, extent.height});
		m_Scene->OnViewportResize(gameSize.x, gameSize.y);
		m_Hierarchy.SetContext(m_Scene);
		AddCamera();
		m_Camera = EditorCamera(45, float(extent.width) / extent.height, .1f, 1000);
		m_Camera.LookAt({ 0, 0, 6 }, { 0, 0, 0 });
		m_ScenePath.clear();
		m_Lighting->Load({});
		m_HasSceneImage = false;
		Report("Created an untitled scene.");
	}

	void VulkanForgeLayer::SaveScene(bool choosePath)
	{
		if (m_EditScene) return;
		auto path = m_ScenePath;
		if (choosePath || path.empty())
			path = FileDialogs::SaveFile("Redflag Scene (*.redflag)\0*.redflag\0");
		if (path.empty()) return;
		if (path.extension().empty()) path += ".redflag";
		bool sceneWritten = false;
		try
		{
			SceneSerializer(m_Scene).Serialize(path.string());
			sceneWritten = true;
			m_Lighting->Save(path, *m_Scene);
			m_ScenePath = path;
			Report("Saved scene and rendering settings to " + path.u8string());
		}
		catch (const std::exception& error)
		{
			std::string message = sceneWritten ? "已写入场景 " + path.u8string() + "，渲染设置或烘焙文件保存失败"
				: "无法保存场景 " + path.u8string();
			if (const auto* fileError = dynamic_cast<const std::filesystem::filesystem_error*>(&error))
			{
				const std::filesystem::path& file = fileError->path2().empty() ? fileError->path1() : fileError->path2();
				if (sceneWritten) message += "：" + file.filename().u8string();
				message += "（系统错误码 " + std::to_string(fileError->code().value()) + "）。";
				if (fileError->code() == std::errc::permission_denied)
					message += "请检查文件是否只读，或是否正被其他程序占用。";
			}
			else
				message += "：" + std::string(error.what());
			Report(message);
		}
	}

	bool VulkanForgeLayer::OpenScene(const std::filesystem::path& path)
	{
		if (path.empty())
			return false;

		// 读到相机组件时就会计算投影。第一次打开场景时面板尚未布局，先借用窗口的像素尺寸。
		auto scene = CreateRef<Scene>();
		auto extent = m_Draw->GetExtent();
		if (extent.width == 0)
			extent = GetWindowContext().GetExtent();
		const glm::uvec2 gameSize = m_GameViewport.Resolution({extent.width, extent.height});
		scene->OnViewportResize(gameSize.x, gameSize.y);

		if (!SceneSerializer(scene).Deserialize(path.string()))
		{
			Report("Could not open scene: " + path.u8string());
			return false;
		}
		if (m_EditScene) StopScene();
		m_Scene = scene;
		m_ScenePath = path;
		m_Hierarchy.SetContext(scene);
		if (Entity primary = scene->GetPrimaryCameraEntity())
		{
			auto& settings = primary.GetComponent<CameraComponent>().Camera;
			auto& transform = primary.GetComponent<TransformComponent>();
			glm::vec3 forward = glm::quat(transform.Rotation) * glm::vec3(0, 0, -1);
			m_Camera = EditorCamera(glm::degrees(settings.GetPerspectiveFOV()), float(extent.width) / extent.height,
				settings.GetPerspectiveNearClip(), settings.GetPerspectiveFarClip());
			m_Camera.LookAt(transform.Translation, transform.Translation + forward * 4.0f);
		}
		auto models = scene->GetRegistry().view<ModelRendererComponent>();
		if (!models.empty()) m_Hierarchy.SetSelectedEntity({ *models.begin(), scene.get() });
		m_HasSceneImage = false;
		try { m_Lighting->Load(path); }
		catch (const std::exception& error) { Report(error.what()); return false; }
		Report("Opened " + path.generic_u8string());
		return true;
	}

	void VulkanForgeLayer::BakeScene(const std::filesystem::path& path)
	{
		if (!OpenScene(path))
			throw std::runtime_error("Cannot bake scene: " + path.u8string());
		m_Lighting->BakeProbes(*m_Scene);
		Report("Saved " + path.u8string() + ".rfprobes");
	}

	void VulkanForgeLayer::ImportModel(const std::filesystem::path& path)
	{
		if (path.empty())
			return;

		try
		{
			AssetID id = AssetManager::Add<Model>(path.string());
			auto model = AssetManager::Load<Model>(id);
			if (!model)
				throw std::runtime_error("无法读取模型：" + path.u8string());

			// 文件编号用于保存场景，模型引用供本次编辑立即绘制。
			Entity entity = m_Scene->CreateEntity(path.stem().u8string());
			auto& component = entity.AddComponent<ModelRendererComponent>();
			component.ModelID = id;
			component.FilePath = path.generic_string();
			component.ModelAsset = model;
			m_Hierarchy.SetSelectedEntity(entity);
			Report("Imported " + path.generic_u8string());
		}
		catch (const std::exception& error)
		{
			Report(error.what());
		}
	}

	// ---- 游戏播放与物理模拟 ----
	void VulkanForgeLayer::PlayGame()
	{
		m_Standalone = m_GameView = true;
		StartScene(true);
	}

	void VulkanForgeLayer::StartScene(bool play)
	{
		Entity selected = m_Hierarchy.GetSelectedEntity();
		m_EditSelection = selected ? selected.GetUUID() : UUID(0);
		m_EditGameView = m_GameView;
		m_EditScene = m_Scene;
		m_Scene = Scene::Copy(m_EditScene);
		m_Playing = play;
		if (play) {
			m_FirstGameTick = true;
			m_Playtest = PlaytestSession::Open(m_ScenePath);
			m_Scene->OnRuntimeStart();
			m_Photos.Connect(GetWindowContext(), m_Scene, m_ScenePath);
			m_SelectGameTab = true;
		} else {
			m_Scene->OnSimulationStart();
		}
		m_Hierarchy.SetContext(m_Scene);
		m_Hierarchy.SetSelectedEntity(m_Scene->GetEntityByUUID(m_EditSelection));
		m_Lighting->ResetHistory();
		m_HasSceneImage = false;
		if (!m_Standalone) ImGui::SetWindowFocus("场景视口");
	}

	void VulkanForgeLayer::StopScene()
	{
		m_Photos.Stop();
		m_Companions.Stop();
		if (m_Playing) m_Scene->OnRuntimeStop();
		else m_Scene->OnSimulationStop();
		m_Playtest.reset();
		m_Playing = false;
		m_Scene = std::move(m_EditScene);
		m_Hierarchy.SetContext(m_Scene);
		m_Hierarchy.SetSelectedEntity(m_Scene->GetEntityByUUID(m_EditSelection));
		m_SelectGameTab = m_EditGameView;
		m_SelectSceneTab = !m_EditGameView;
		m_Lighting->ResetHistory();
		m_HasSceneImage = false;
	}

	void VulkanForgeLayer::OnUpdate(Timestep ts)
	{
		if (m_Playing) {
			if (m_Playtest) m_Playtest->BeginFrame(ts.GetSeconds());
			auto hud = m_Scene->GetRegistry().view<GameHud>();
			// 首张游戏画面需要上传模型。玩家能看见关卡以后，再开始计算移动和救援时间。
			if ((!m_Standalone || m_HasSceneImage) && (hud.empty() || !hud.get<GameHud>(hud.front()).Finished))
			{
				m_Scene->UpdateGame(m_FirstGameTick ? Timestep(0.0f) : ts);
				m_FirstGameTick = false;
			}
			if (m_Playtest) m_Playtest->RecordScene(*m_Scene);
		}
		else if (m_EditScene)
		{
			m_Scene->OnUpdateAnimations(ts, ReadCharacterMovement(m_ViewportFocused));
			m_Scene->OnUpdatePhysics(ts);
		}
		else
			m_Scene->OnUpdateAnimations(ts);
		if (m_Hovered && !m_GameView)
			m_Camera.OnUpdate(ts);
	}

	void VulkanForgeLayer::OnEvent(Event& event)
	{
		if (m_Hovered && !m_GameView)
			m_Camera.OnEvent(event);
	}

	// ---- 编辑器窗口 ----
	void VulkanForgeLayer::DrawMenu()
	{
		if (!ImGui::BeginMenuBar()) return;
		if (ImGui::BeginMenu("文件"))
		{
			if (ImGui::MenuItem("新建场景")) NewScene();
			if (ImGui::MenuItem("打开场景…")) OpenScene(FileDialogs::OpenFile("Redflag Scene (*.redflag)\0*.redflag\0"));
			if (ImGui::MenuItem("导入模型…"))
				ImportModel(FileDialogs::OpenFile("3D Model (*.obj;*.fbx;*.gltf;*.glb;*.ply)\0*.obj;*.fbx;*.gltf;*.glb;*.ply\0"));
			ImGui::Separator();
			if (ImGui::MenuItem("保存", "Ctrl+S", false, !m_EditScene)) SaveScene(false);
			if (ImGui::MenuItem("另存为…", "Ctrl+Shift+S", false, !m_EditScene)) SaveScene(true);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("环境贴图"))
		{
			bool visible = m_Scene->IsSkyVisible();
			if (ImGui::MenuItem("显示天空背景", nullptr, &visible))
				m_Scene->SetSkyVisible(visible);
			if (ImGui::MenuItem("使用默认天空"))
				m_Scene->SetEnvironment(DefaultSky);
			if (ImGui::MenuItem("选择 HDR 或烘焙文件…"))
			{
				auto path = FileDialogs::OpenFile("Environment (*.hdr;*.rfibl)\0*.hdr;*.rfibl\0");
				if (!path.empty()) m_Scene->SetEnvironment(std::filesystem::path(path));
			}
			if (ImGui::MenuItem("移除环境贴图", nullptr, false, !m_Scene->GetEnvironment().empty()))
				m_Scene->SetEnvironment({});
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("编辑"))
		{
			Entity selected = m_Hierarchy.GetSelectedEntity();
			if (ImGui::MenuItem("复制选中对象", nullptr, false, bool(selected)))
			{
				m_Scene->DuplicateEntity(selected);
				Report("Duplicated " + selected.GetName());
			}
			if (ImGui::MenuItem("删除选中对象", nullptr, false, bool(selected)))
			{
				Report("Deleted " + selected.GetName());
				m_Hierarchy.SetSelectedEntity({});
				m_Scene->DestroyEntity(selected);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("场景对象"))
		{
			if (ImGui::MenuItem("空对象")) m_Hierarchy.SetSelectedEntity(m_Scene->CreateEntity("空对象"));
			if (ImGui::MenuItem("立方体"))
			{
				auto cube = m_Scene->CreateEntity("立方体");
				cube.AddComponent<MeshRendererComponent>().MeshData = Mesh::CreateCube();
				m_Hierarchy.SetSelectedEntity(cube);
			}
			if (ImGui::MenuItem("点光源"))
			{
				auto lamp = m_Scene->CreateEntity("点光源");
				lamp.AddComponent<PointLightComponent>().Intensity = 5;
				lamp.GetComponent<TransformComponent>().Translation.y = 1;
				m_Hierarchy.SetSelectedEntity(lamp);
			}
			if (ImGui::MenuItem("相机")) AddCamera();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("窗口"))
		{
			m_Reviews.DrawMenu();
			m_DevContext.DrawMenu();
			if (ImGui::MenuItem("恢复默认布局")) m_ResetLayout = true;
			ImGui::EndMenu();
		}
		ImGui::Separator();
		const bool shortcuts = !ImGui::GetIO().WantTextInput;
		if (!m_EditScene) {
			if (ImGui::MenuItem("播放", "F6") || (shortcuts && ImGui::IsKeyPressed(ImGuiKey_F6, false)))
				StartScene(true);
			else if (ImGui::MenuItem("模拟物理", "F5") || (shortcuts && ImGui::IsKeyPressed(ImGuiKey_F5, false)))
				StartScene(false);
		} else if (ImGui::MenuItem(m_Playing ? "停止播放" : "停止模拟", m_Playing ? "F6" : "F5") ||
			(shortcuts && ImGui::IsKeyPressed(m_Playing ? ImGuiKey_F6 : ImGuiKey_F5, false))) {
			StopScene();
		}
		m_Decisions.Draw(m_Scene);
		m_Companions.DrawMenu();
		ImGui::TextDisabled("%s   /   Vulkan", m_ScenePath.empty() ? "未命名" : m_ScenePath.stem().u8string().c_str());
		ImGui::EndMenuBar();
		if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
			SaveScene(ImGui::GetIO().KeyShift);
	}

	// ImGui 默认在缩小主窗口时保持侧栏的像素宽度。按原比例一起缩放，给中间场景留出空间。
	static void ResizeDock(ImGuiDockNode* node, const ImVec2& ratio)
	{
		for (auto* child : node->ChildNodes)
		{
			if (!child) continue;
			child->SizeRef.x *= ratio.x;
			child->SizeRef.y *= ratio.y;
			ResizeDock(child, ratio);
		}
	}

	void VulkanForgeLayer::OnImGuiRender()
	{
		m_Photos.Update();
		const auto photoPanel = [&](uint64_t actor) { m_Photos.Draw(GetWindowContext(), m_Scene, m_ScenePath, actor); };
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		if (m_Standalone) {
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
			DrawViewport();
			ImGui::PopStyleVar();
			m_Companions.Draw(*m_Scene, m_Playing, photoPanel);
			return;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::Begin("Vulkan Workspace", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus);
		ImGui::PopStyleVar(2);
		DrawMenu();

		// 默认布局把属性放在右上，渲染设置放在右下。底部用标签页切换项目和控制台。
		ImGuiID dock = ImGui::GetID("VulkanEditorDockV3");
		const auto available = ImGui::GetContentRegionAvail();
		if (m_ResetLayout)
		{
			ImGui::DockBuilderRemoveNode(dock);
			m_ResetLayout = false;
		}
		auto* node = ImGui::DockBuilderGetNode(dock);
		if (!node)
		{
			ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dock, available);
			ImGuiID work, right, project, upper, hierarchy, scene, inspector, lighting;
			ImGui::DockBuilderSplitNode(dock, ImGuiDir_Right, .28f, &right, &work);
			ImGui::DockBuilderSplitNode(work, ImGuiDir_Down, .23f, &project, &upper);
			ImGui::DockBuilderSplitNode(upper, ImGuiDir_Left, .23f, &hierarchy, &scene);
			ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, .48f, &lighting, &inspector);
			ImGui::DockBuilderDockWindow("层级  Hierarchy", hierarchy);
			ImGui::DockBuilderDockWindow("场景视口", scene);
			ImGui::DockBuilderDockWindow("属性  Inspector", inspector);
			ImGui::DockBuilderDockWindow("渲染设置  Lighting", lighting);
			ImGui::DockBuilderDockWindow("项目  Project", project);
			ImGui::DockBuilderDockWindow("控制台  Console", project);
			ImGui::DockBuilderFinish(dock);
		}
		else if (node->Size.x > 0 && node->Size.y > 0 && (available.x != node->Size.x || available.y != node->Size.y))
			ResizeDock(node, { available.x / node->Size.x, available.y / node->Size.y });
		ImGui::DockSpace(dock);
		ImGui::End();

		m_Hierarchy.OnImguiRender("层级  Hierarchy", "属性  Inspector");
        Entity actor = m_Hierarchy.GetSelectedEntity();
        if (actor && actor.HasComponent<BehaviorTreeComponent>()) {
            auto& behavior = m_Scene->GetRegistry().get_or_emplace<ActorBehavior>(static_cast<entt::entity>(actor));
            if (behavior.Open(actor.GetComponent<BehaviorTreeComponent>().File))
                m_ActorTree.Draw(behavior.Tree, std::filesystem::u8path(behavior.File), &behavior.Trace, "角色行为树");
        }

		if (ImGui::Begin("渲染设置  Lighting")) m_Lighting->DrawSettings(!m_EditScene, !m_GameView);
		ImGui::End();
		auto asset = m_Project.Draw();
		if (!asset.empty())
		{
			if (asset.extension() == ".redflag") OpenScene(asset);
			else ImportModel(asset);
		}
		if (ImGui::Begin("控制台  Console", nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
		{
			if (ImGui::SmallButton("清空记录")) m_Messages.clear();
			ImGui::Separator();
			for (auto it = m_Messages.rbegin(); it != m_Messages.rend(); ++it)
				ImGui::TextWrapped("%s", it->c_str());
		}
		ImGui::End();
		DrawViewport();
		m_Companions.Draw(*m_Scene, m_Playing, photoPanel);
		m_Reviews.Draw(GetWindowContext(), *m_Draw);
		m_DevContext.Draw();
	}

	void VulkanForgeLayer::DrawTransformTools()
	{
		const char* names[] = { "选择", "移动", "旋转", "缩放" };
		const ImGuiKey keys[] = { ImGuiKey_Q, ImGuiKey_W, ImGuiKey_E, ImGuiKey_R };
		const char* shortcuts[] = { "Q", "W", "E", "R" };
		for (int i = 0; i < 4; ++i)
		{
			if (i) ImGui::SameLine();
			bool active = m_TransformTool == i;
			if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
			if (ImGui::Button(names[i]) || (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(keys[i], false)))
				m_TransformTool = i;
			if (active) ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s工具 (%s)", names[i], shortcuts[i]);
		}
	}

	bool VulkanForgeLayer::DrawSceneGizmos(const glm::mat4& projection, const glm::mat4& view, ImVec2 corner, ImVec2 size)
	{
		Entity selected = m_Hierarchy.GetSelectedEntity();
		bool handled = false;
		if (selected && m_TransformTool && !ImGui::GetIO().KeyAlt)
		{
			const ImGuizmo::OPERATION tools[] = { ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE };
			auto& transform = selected.GetComponent<TransformComponent>();
			auto world = transform.GetTransform();
			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist();
			ImGuizmo::SetRect(corner.x, corner.y, size.x, size.y);
			ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), tools[m_TransformTool-1], ImGuizmo::LOCAL, glm::value_ptr(world));
			if (ImGuizmo::IsUsing())
				Math::DecomposeTransform(world, transform.Translation, transform.Rotation, transform.Scale);
			handled = ImGuizmo::IsUsing() || ImGuizmo::IsOver();
		}
		if (!m_ShowLights) return handled;

		// 灯本身没有可点选的网格。在编辑视图用一个圆标出灯的位置，主相机画面不画这些标记。
		auto* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(corner, { corner.x + size.x, corner.y + size.y }, true);
		const auto mouse = ImGui::GetMousePos();
		const float radius = ImGui::GetFontSize() * .29f;
		auto lights = m_Scene->GetRegistry().view<TransformComponent, PointLightComponent>();
		for (auto id : lights)
		{
			const auto& position = lights.get<TransformComponent>(id).Translation;
			glm::vec4 clip = projection * view * glm::vec4(position, 1);
			if (clip.w <= 0 || std::abs(clip.x) > clip.w || std::abs(clip.y) > clip.w || std::abs(clip.z) > clip.w) continue;
			ImVec2 pixel{ corner.x + (.5f + .5f*clip.x/clip.w)*size.x, corner.y + (.5f - .5f*clip.y/clip.w)*size.y };
			Entity lamp{ id, m_Scene.get() };
			const auto ring = lamp == selected ? IM_COL32(255,249,236,255) : IM_COL32(235,195,127,255);
			draw->AddCircleFilled(pixel, radius + 2, IM_COL32(56,45,36,220), 20);
			draw->AddCircle(pixel, radius, ring, 20, 2);
			draw->AddCircleFilled(pixel, radius*.36f, ring, 12);
			const float dx = mouse.x - pixel.x, dy = mouse.y - pixel.y;
			if (m_Hovered && !handled && dx*dx + dy*dy < (radius+3)*(radius+3))
			{
				ImGui::SetTooltip("%s", lamp.GetName().c_str());
				if (ImGui::IsMouseClicked(0) && !ImGui::GetIO().KeyAlt) m_Hierarchy.SetSelectedEntity(lamp);
				handled = true;
			}
		}
		draw->PopClipRect();
		return handled;
	}

	void VulkanForgeLayer::DrawViewport()
	{
		m_Hovered = false;
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
		if (m_Standalone)
		{
			// 游戏画面始终在工具窗口下面。点回游戏后，队友通讯仍能接收下一次点击。
			flags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
				ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
			const ImVec2 window = ImGui::GetMainViewport()->WorkSize;
			const ImVec2 margin = m_GameViewport.Fit(window, ImGui::GetIO().DisplayFramebufferScale, true).Offset;
			// 场景铺满窗口时省去被它盖住的底色。留有黑边的窗口继续按主题绘制。
			if (margin.x == 0.f && margin.y == 0.f) ImGui::SetNextWindowBgAlpha(0.f);
		}
		bool visible = ImGui::Begin(m_Standalone ? "Redflag Player" : "场景视口", nullptr, flags);
		m_ViewportFocused = visible && ImGui::IsWindowFocused();
		if (m_Playing && m_GameView && m_ViewportFocused && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
			StopScene();
			StartScene(true);
		}
		if (visible && !m_Standalone && ImGui::BeginTabBar("Cameras"))
		{
			const bool previous = m_GameView;
			if (ImGui::BeginTabItem("场景 Scene", nullptr, m_SelectSceneTab ? ImGuiTabItemFlags_SetSelected : 0))
			{
				m_GameView = false;
				m_SelectSceneTab = false;
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("主相机 Game", nullptr, m_SelectGameTab ? ImGuiTabItemFlags_SetSelected : 0)) {
				m_GameView = true;
				m_SelectGameTab = false;
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
			if (m_GameView != previous) { m_Lighting->ResetHistory(); m_HasSceneImage = false; }
			if (!m_GameView) { DrawTransformTools(); ImGui::SameLine(); }
			m_Lighting->DrawEffectsMenu();
			if (m_GameView) { ImGui::SameLine(); m_GameViewport.DrawResolution(); }
			if (!m_GameView)
			{
				ImGui::SameLine();
				ImGui::Checkbox("灯标记", &m_ShowLights);
			}
			Entity selected = m_Hierarchy.GetSelectedEntity();
			ImGui::TextDisabled("%s  /  %s", m_GameView ? "场景主相机" : "编辑相机",
				m_Navigating ? "移动中 · 直接光" : (m_Lighting->CompareDirect ? "仅直接光对照" : LightingPreview::ModeName(m_Lighting->Mode)));
			if (!m_GameView && selected)
			{
				ImGui::SameLine();
				ImGui::Text("  ·  %s", selected.GetName().c_str());
			}
			if (!m_GameView && ImGui::IsItemHovered()) ImGui::SetTooltip("Alt + 左键旋转，Alt + 中键平移，滚轮缩放。");
		}

		Entity gameCamera = m_Scene->GetPrimaryCameraEntity();
		if (visible && m_GameView && !gameCamera)
		{
			ImGui::TextWrapped("没有主相机。可在「场景对象」菜单中创建相机。");
			visible = false;
		}
		const ImVec2 available = ImGui::GetContentRegionAvail();
		if (visible && available.x >= 1 && available.y >= 1)
		{
			const GameViewport::Layout layout = m_GameViewport.Fit(available, ImGui::GetIO().DisplayFramebufferScale, m_GameView);
			const uint32_t width = layout.Pixels.x, height = layout.Pixels.y;
			const ImVec2 size = layout.Size;
			if (m_Draw->Resize(width, height))
			{
				// Resize 已等显卡用完旧图，现在可把新的场景贴图登记给 ImGui。
				if (m_PanelTexture) ImGui_ImplVulkan_RemoveTexture(m_PanelTexture);
				m_PanelTexture = ImGui_ImplVulkan_AddTexture(m_Draw->GetSampler(), m_Draw->GetColorView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				m_Camera.SetViewportSize(float(width), float(height));
				const glm::uvec2 gameSize = m_GameViewport.Resolution(layout.Pixels);
				m_Scene->OnViewportResize(gameSize.x, gameSize.y);
				m_HasSceneImage = false;
			}

			glm::mat4 view = m_Camera.GetViewMatrix(), projection = m_Camera.GetProjection();
			glm::vec3 eye = m_Camera.GetPosition();
			if (m_GameView)
			{
				auto& transform = gameCamera.GetComponent<TransformComponent>();
				view = glm::inverse(transform.GetTransform());
				eye = transform.Translation;
				projection = gameCamera.GetComponent<CameraComponent>().Camera.GetProjection();
			}
			// 开启“移动时仅算直接光”后，镜头停下 0.22 秒才恢复原来的光照效果。
			// 这个时长是试用滚轮后选的，给两次滚动留一点间隔，免得画面一亮一暗。
			constexpr double wheelPauseSeconds = 0.22;
			const double now = ImGui::GetTime();
			if (!m_GameView && m_HasSceneImage && view != m_LastView)
				m_NavigationUntil = now + wheelPauseSeconds;
			m_LastView = view;
			const bool navigate = !m_GameView && m_Lighting->FastNavigation && m_Lighting->Mode == Shading::Lit && now < m_NavigationUntil;
			if (m_Navigating != navigate) m_Lighting->ResetHistory();
			m_Navigating = navigate;
			VkDescriptorSet image = m_PanelTexture;
			bool overlayModels = false;
			if (m_Lighting->Mode != Shading::BaseColor)
			{
				VkDescriptorSet light = m_Lighting->Render(*m_Scene, projection, view, eye, width, height, navigate, !m_GameView);
				if (light)
				{
					image = light;
					overlayModels = m_Lighting->NeedsModelOverlay();
				}
			}
			ImVec2 origin = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos({origin.x + layout.Offset.x, origin.y + layout.Offset.y});
			ImGui::Image((ImTextureID)image, size);
			if (overlayModels)
				ImGui::GetWindowDrawList()->AddImage((ImTextureID)m_PanelTexture, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			m_Hovered = ImGui::IsItemHovered();
			const bool clicked = ImGui::IsItemClicked();
			const auto corner = ImGui::GetItemRectMin();
			if (m_GameView && m_Playing) m_Hud.Draw(*m_Scene, *m_Draw, projection * view, corner, size);
			bool gizmo = !m_GameView && DrawSceneGizmos(projection, view, corner, size);
			if (!m_GameView && !gizmo && m_HasSceneImage && clicked && !ImGui::GetIO().KeyAlt)
			{
				// 鼠标点的是刚显示过的那一帧，编号也从同一帧读。界面坐标先换成图像像素。
				ImVec2 mouse = ImGui::GetMousePos();
				const auto x = (std::min)(width - 1, uint32_t((mouse.x - corner.x) * width / size.x));
				const auto y = (std::min)(height - 1, uint32_t((mouse.y - corner.y) * height / size.y));
				auto pixel = m_Draw->ReadPixel(x, y);
				auto id = static_cast<entt::entity>(pixel.EntityID);
				m_Hierarchy.SetSelectedEntity(pixel.EntityID != -1 && m_Scene->GetRegistry().valid(id)
					? Entity{ id, m_Scene.get() } : Entity{});
			}
			// 点击用的物体编号已从上一张图读完，现在可以画新一帧的编号。ImGui 在帧末显示上面选好的颜色图。
			m_Draw->BeginScene(overlayModels);
			SceneRenderer::DrawGeometry(*m_Scene, *m_Draw, projection * view);
			m_Draw->EndScene();
			m_HasSceneImage = true;
		}
		Application::Get().GetImGuiLayer()->BlockEvents(!m_Hovered);
		ImGui::End();
	}
}
