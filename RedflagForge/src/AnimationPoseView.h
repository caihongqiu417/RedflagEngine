#pragma once

#include "Redflag/Animation/AnimationController.h"

namespace Redflag
{
    class ModelAnimation;

    void DrawAnimationPose(const ModelAnimation& model, const std::string& savedClip,
                           const std::vector<AnimationMarker>& savedMarkers, const std::string& draftClip,
                           const AnimationMarker& marker, bool atEnd);
}
