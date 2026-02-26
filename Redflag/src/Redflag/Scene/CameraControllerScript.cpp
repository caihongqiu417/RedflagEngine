#include "rfpch.h"
#include "CameraControllerScript.h"
#include "Components.h"
#include "Scene.h"    
#include"glm/gtc/matrix_transform.hpp"

namespace Redflag {

    void CameraControllerScript::OnCreate()
    {
        m_CameraSpeed = 5.0f;
        auto& transform = GetComponent<TransformComponent>();
       
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

        // Q/E 旋转相机（绕Z轴旋转）
        if (Input::IsKeyPressed(RF_KEY_Q))
        {
            // 绕Z轴逆时针旋转（角度制）
            transform.Transform = glm::rotate(transform.Transform, glm::radians(speed * 30.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        }
        if (Input::IsKeyPressed(RF_KEY_E))
        {
            // 绕Z轴顺时针旋转
            transform.Transform = glm::rotate(transform.Transform, glm::radians(-speed * 30.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        }

        // 临时用 R/F 代替滚轮
        if (Input::IsKeyPressed(RF_KEY_R))
        {
            // 放大
            auto& camera = GetComponent<CameraComponent>();
            float size = camera.Camera.GetOrthographicSize();
            camera.Camera.SetOrthographicSize(size - speed * 3.f);
        }
        if (Input::IsKeyPressed(RF_KEY_F))
        {
            // 缩小
            auto& camera = GetComponent<CameraComponent>();
            float size = camera.Camera.GetOrthographicSize();
            camera.Camera.SetOrthographicSize(size + speed * 3.f);
        }
    }

}