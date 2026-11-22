#pragma once

#include <array>
#include <optional>

#include "Redflag/Core/Layer.h"

#include"Redflag/Events/Event.h"
#include"Redflag/Events/KeyEvent.h"
#include"Redflag/Events/MouseEvent.h"
#include"Redflag/Events/ApplicationEvent.h"

namespace Redflag
{
	class VulkanImGui;

	class REDFLAG_API ImGuiLayer :public Layer
	{
	public:
		ImGuiLayer();
		~ImGuiLayer();


		virtual void OnAttach() override;      // 初始化ImGui上下文和配置
		virtual void OnDetach() override;      // 清理ImGui资源
		virtual void OnImGuiRender() override; // 渲染ImGui UI内容
		virtual void OnEvent(Event& event) override;

		void BlockEvents(bool block);
		void ReleaseGameMouse();
		bool IsGameMouseDown(int button) const;

		void Begin();  // 开始一个新的ImGui帧（必须在渲染前调用）
		void End();    // 结束当前ImGui帧（必须在渲染后调用）

		void SetThemeColor();
	private:
		Scope<VulkanImGui> m_Vulkan;
		std::optional<std::array<bool, 3>> m_GameMouseButtons;
		bool m_BlockEvents = true;  // 默认拦截事件
	};
}