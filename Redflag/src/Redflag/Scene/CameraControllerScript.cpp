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
        auto& cameraComponent = GetComponent<CameraComponent>();
        // 只修改主相机
        if (!cameraComponent.Primary)
            return;  // 如果不是主相机，直接返回，不执行任何操作


        auto& transform = GetComponent<TransformComponent>();
        float speed = m_CameraSpeed * ts.GetSeconds();

        if (Input::IsKeyPressed(RF_KEY_W))
        {
            transform.Translation.y += speed;  
        }
        if (Input::IsKeyPressed(RF_KEY_S))
        {
            transform.Translation.y -= speed;  
        }
        if (Input::IsKeyPressed(RF_KEY_A))
        {
            transform.Translation.x -= speed;  
        }
        if (Input::IsKeyPressed(RF_KEY_D))
        {
            transform.Translation.x += speed;  
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