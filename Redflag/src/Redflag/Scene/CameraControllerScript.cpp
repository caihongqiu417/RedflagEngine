#include "rfpch.h"
#include "CameraControllerScript.h"
#include "Components.h"
#include "Scene.h"    

namespace Redflag {

    void CameraControllerScript::OnCreate()
    {
        m_CameraSpeed = 5.0f;
        std::cout << "Camera Controller Created" << std::endl;
    }

    void CameraControllerScript::OnDestroy()
    {
        std::cout << "Camera Controller Destroyed" << std::endl;
    }

    void CameraControllerScript::OnUpdate(Timestep ts)
    {
        auto& transform = GetComponent<TransformComponent>();
        float speed = m_CameraSpeed * ts.GetSeconds();

        // WASD 移动相机 - 修改位置矩阵的最后一列（平移部分）
        // glm::mat4 是列主序，transform[3] 是第4列（平移向量）
        if (Input::IsKeyPressed(RF_KEY_W))
        {
            transform.Transform[3][1] += speed;  // 第4列，第2行 = Y轴
        }
        if (Input::IsKeyPressed(RF_KEY_S))
        {
            transform.Transform[3][1] -= speed;  // Y轴
        }
        if (Input::IsKeyPressed(RF_KEY_A))
        {
            transform.Transform[3][0] -= speed;  // 第4列，第1行 = X轴
        }
        if (Input::IsKeyPressed(RF_KEY_D))
        {
            transform.Transform[3][0] += speed;  // X轴
        }

        // Q/E 缩放相机
        auto& camera = GetComponent<CameraComponent>();
        if (Input::IsKeyPressed(RF_KEY_Q))
        {
            float size = camera.Camera.GetOrthographicSize();
            camera.Camera.SetOrthographicSize(size - speed);
        }
        if (Input::IsKeyPressed(RF_KEY_E))
        {
            float size = camera.Camera.GetOrthographicSize();
            camera.Camera.SetOrthographicSize(size + speed);
        }
    }

}