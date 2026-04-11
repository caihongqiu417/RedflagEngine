#pragma once


//-------------Core----------------
#include "Redflag/Core/Application.h"

#include"Redflag/Core/Layer.h"
#include"Redflag/Core/Log.h"

#include"Redflag/Core/TimeStep.h"

#include"Redflag/ImGui/ImGuiLayer.h"

#include"Redflag/Core/KeyCode.h"
#include"Redflag/Core/MouseButtonCode.h"
#include"Redflag/Core/Input.h"
//-------------Core----------------


//-------------Renderer----------------
#include"Redflag/Renderer/Renderer.h"
#include"Redflag/Renderer/Renderer2D.h"
#include"Redflag/Renderer/Renderer3D.h"
#include"Redflag/Renderer/Mesh.h"
#include"Redflag/Renderer/RenderCommand.h"

#include"Redflag/Renderer/Shader.h"
#include"Redflag/Renderer/Buffer.h"
#include"Redflag/Renderer/Texture.h"
#include"Redflag/Renderer/SubTexture2D.h"
#include"Redflag/Renderer/VertexArray.h"

#include"Redflag/Renderer/OrthographicCamera.h"
#include"Redflag/Renderer/OrthographicCameraController.h"

#include"Redflag/Renderer/Framebuffer.h"
//-------------------------------------

//-------------Particle----------------
#include"Redflag/ParticleSystem/ParticleSystem.h"
//-------------------------------------


//----------------ECS------------------
#include"Redflag/Scene/Entity.h"           // 1. �������Entity
#include"Redflag/Scene/ScriptableEntity.h"  // 2. ����Entity
#include"Redflag/Scene/Components.h"        // 3. ����Entity��ScriptableEntity
#include"Redflag/Scene/Scene.h"             // 4. ������������
//-------------------------------------


