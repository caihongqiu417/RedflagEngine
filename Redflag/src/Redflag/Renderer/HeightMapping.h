#pragma once

namespace Redflag
{
	// 凹凸改变光照法线，视差另找贴图采样位置，顶点置换实际移动网格。
	enum class HeightMapping { Off, Bump, Parallax, Displacement };
}
