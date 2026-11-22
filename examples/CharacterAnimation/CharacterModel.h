#pragma once

#include "Redflag/Animation/SkinnedMesh.h"

// 示例的身体按 BVH 骨长生成，便于观察膝、肘附近两个关节共同影响的顶点。
Redflag::SkinnedMesh MakeCharacterModel(const Redflag::Skeleton& rig);
