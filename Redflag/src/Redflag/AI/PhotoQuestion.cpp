#include "rfpch.h"
#include "PhotoQuestion.h"
#include "NpcSystem.h"
#include "Redflag/Scene/Entity.h"

namespace Redflag
{
	glm::vec3 NpcEyePosition(const TransformComponent& transform, const NpcDefinition& npc)
	{
		return transform.Translation + glm::quat(transform.Rotation) * glm::vec3(0,npc.EyeHeight,npc.EyeForward);
	}

	bool PhotoQuestion::Start(const Ref<Scene>& scene, NpcPhoto photo, PhotoSubject subject, const std::string& question)
	{
		Clear();
		NpcSystem* npcs = scene->GetNpcs();
		NpcDefinition* npc = npcs ? npcs->GetDefinition(photo.Observer) : nullptr;
		if (!npc || !npc->Memory.Pictures || photo.Image.Bytes.empty() || question.empty())
			return false;
		m_Scene = scene;
		m_Session = npcs->SessionStart();
		m_Photo = std::move(photo);
		m_Subject = subject;
		m_SearchText = question;

		DecisionRequest request;
		request.Model = npc->Model;
		request.Prompt = question;
		request.Instructions = "Use the attached photographs only. For an object, inspect each individual close-up. If none shows the requested object, choose absent.";
		request.Images.push_back(m_Photo.Image);
		if (subject == PhotoSubject::Object)
		{
			for (const PhotoRegion& region : m_Photo.Regions)
			{
				request.Images.push_back(region.Closeup);
				request.Candidates.push_back({region.Closeup.ID, "object in close-up image " + region.Closeup.ID});
			}
			request.Candidates.push_back({"absent", "none of the photographed objects matches the request"});
		}
		if (subject == PhotoSubject::Lighting)
			request.Candidates = {{"lit", "brightly illuminated surfaces"}, {"dark", "dark or unlit surfaces"}, {"uncertain", "unclear or mixed illumination"}};
		m_Request = subject == PhotoSubject::Description ? m_Client.StartReply(std::move(request)) : m_Client.Start(std::move(request));
		return true;
	}

	bool PhotoQuestion::Update()
	{
		if (!m_Request) return false;
		const auto result = m_Client.Take(m_Request);
		if (!result) return false;
		m_Request = 0;
		if (result->Status != DecisionStatus::Completed) { m_Receipt = result->Error; return true; }
		const Ref<Scene> scene = m_Scene.lock();
		NpcSystem* npcs = scene ? scene->GetNpcs() : nullptr;
		const NpcDefinition* npc = npcs && npcs->SessionStart() == m_Session ? npcs->GetDefinition(m_Photo.Observer) : nullptr;
		if (!npc || !npc->Memory.Pictures) { m_Receipt = "本次观察已结束"; return true; }
		if (!m_Verifying)
			m_Answer = m_Subject == PhotoSubject::Description ? result->Text : result->Choice;
		else if (result->Choice == "absent")
		{
			m_Target = 0;
			m_Answer = "absent";
		}

		if (m_Subject == PhotoSubject::Object && !m_Verifying && m_Answer != "absent")
		{
			// 仅凭总览，模型有时把墙面误认为墙前已拿走的物品。让它单独检查选中物体的像素。
			const auto selected = std::find_if(m_Photo.Regions.begin(),m_Photo.Regions.end(),
				[&](const PhotoRegion& r) { return r.Closeup.ID == m_Answer; });
			m_Target = selected->Entity;
			DecisionRequest check;
			check.Model = npc->Model;
			check.Prompt = "Does this single object close-up show the object requested below? If the object is not actually visible, select absent. Original question: " + m_SearchText;
			check.Instructions = "Look at the pixels of the close-up only.";
			check.Images.push_back(selected->Closeup);
			check.Candidates = {{"absent","the object sought is missing, different, or unclear"},{"present","this visible object matches the object being sought"}};
			m_Verifying = true;
			m_Request = m_Client.Start(std::move(check));
			return false;
		}

		std::string evidence = m_Answer;
		if (m_Subject == PhotoSubject::Object && m_Answer != "absent")
		{
			Entity target = scene->GetEntityByUUID(m_Target);
			if (!target) { m_Receipt = "照片中的物体已离开场景"; return true; }
			const Entity observer = scene->GetEntityByUUID(m_Photo.Observer);
			const glm::vec3 eye = NpcEyePosition(observer.GetComponent<TransformComponent>(), *npc);
			const glm::vec3 delta = target.GetComponent<TransformComponent>().Translation - eye;
			const float distance = glm::length(delta);
			const auto hit = scene->Raycast(eye, delta, distance, m_Photo.Observer);
			if (distance > npc->Sight || (hit && hit->Entity != m_Target)) { m_Receipt = "当前位置看不到该物体"; return true; }
			evidence = target.GetName();
			m_Receipt = "当前可见";
			if (target.HasComponent<PickupComponent>())
			{
				const PickupComponent& item = target.GetComponent<PickupComponent>();
				if (!item.Item.empty() && item.Count > 0 && CanReach(*scene,m_Photo.Observer,m_Target,npc->Reach))
					m_Receipt = "当前在拿取范围内";
			}
		}
		else if (m_Subject == PhotoSubject::Object) m_Receipt = evidence = "照片里没有找到目标";
		else m_Receipt = "已记录拍摄时的画面";
		npcs->GetMemory(m_Photo.Observer)->RememberPhoto(m_Target, evidence, m_Photo.Seconds, m_Photo.Image.ID);
		return true;
	}

	void PhotoQuestion::Clear()
	{
		m_Client.Clear();
		m_Request = m_Target = 0;
		m_Verifying = false;
		m_SearchText.clear();
		m_Photo = {};
		m_Scene.reset();
		m_Answer.clear();
		m_Receipt.clear();
	}
}
