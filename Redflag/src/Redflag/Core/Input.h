#pragma once

#include "Redflag/Core/Core.h"

namespace Redflag
{
	class REDFLAG_API Input
	{
	public:
		static bool IsKeyPressed(int keycode);
		
		static bool IsMouseButtonPressed(int button);
		static void CaptureCursor(bool capture);
		static std::pair<float, float> GetMousePosition();
		static float GetMouseX();
		static float GetMouseY();
	};
}
