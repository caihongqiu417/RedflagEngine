#pragma once
#include "Redflag/Scene/ScriptableEntity.h"
#include "Redflag/Core/Input.h"
#include "Redflag/Core/KeyCode.h"

namespace Redflag {

    class CameraControllerScript : public ScriptableEntity
    {
    public:
        void OnCreate();      // 不需要 override
        void OnDestroy();     // 不需要 override
        void OnUpdate(Timestep ts);  // 不需要 override

    private:
        float m_CameraSpeed = 5.0f;
    };

}