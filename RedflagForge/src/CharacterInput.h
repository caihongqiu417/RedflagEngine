#pragma once

#include "Redflag/Core/Input.h"
#include "Redflag/Core/KeyCode.h"
#include <glm/vec2.hpp>
#include <imgui.h>

namespace Redflag
{
	inline glm::vec2 ReadCharacterMovement(bool viewportActive)
	{
		if (!viewportActive || ImGui::GetIO().WantTextInput) return glm::vec2(0);
		return {float(Input::IsKeyPressed(RF_KEY_RIGHT) - Input::IsKeyPressed(RF_KEY_LEFT)),
		        float(Input::IsKeyPressed(RF_KEY_DOWN) - Input::IsKeyPressed(RF_KEY_UP))};
	}
}
