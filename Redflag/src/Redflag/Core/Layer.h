#pragma once

#include"Redflag/Core/Core.h"
#include"Redflag/Events/Event.h"
#include"Core/Timestep.h"
#include"imgui.h"


namespace Redflag
{
	class REDFLAG_API Layer
	{
	public:
		Layer(const std::string& name = "Layer");
		virtual ~Layer();

		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnUpdate(Timestep timestep) {}     // 每帧更新时调用（游戏逻辑）
		virtual void OnImGuiRender() {} // 每帧渲染GUI时调用（UI逻辑）
		virtual void OnEvent(Event& event) {}

		inline const std::string& GetName() const { return m_DebugName; }
	protected:
		std::string m_DebugName;
	};
}