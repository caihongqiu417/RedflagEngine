#pragma once

#include "DecisionClient.h"

namespace Redflag
{
	class Scene;

	/// <summary>读取实体此刻所在的环境区域及关联灯具。地貌文字由作者填写，灯的亮度从当前场景读取。</summary>
	std::vector<DecisionFact> SenseEnvironment(Scene& scene, uint64_t actor);
}
