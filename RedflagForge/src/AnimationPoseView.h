#pragma once

#include "Redflag/Animation/AnimationController.h"

namespace Redflag
{
    class ModelAnimation;

    void DrawAnimationPose(const ModelAnimation& model, const AnimationState& saved, const AnimationState& draft,
                           const AnimationMarker& marker, bool atEnd);
}
