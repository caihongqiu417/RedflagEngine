#include "rfpch.h"
#include "PhotoQuestion.h"
#include "NpcSystem.h"
#include "Redflag/Scene/Entity.h"

namespace Redflag
{
	//========== 拍照时的视角与问题 ==========

	glm::vec3 NpcEyePosition(const TransformComponent& transform, const NpcDefinition& npc)
	{
		return transform.Translation + glm::quat(transform.Rotation) * glm::vec3(0,npc.EyeHeight,npc.EyeForward);
	}

	bool PhotoQuestion::Start(const Ref<Scene>& scene, NpcPhoto photo, PhotoSubject subject, const std::string& question, bool suggestPlace)
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
		m_SuggestPlace = suggestPlace;

		DecisionRequest request;
		request.Model = npc->Model;
		request.Prompt = question;
		request.Instructions = "Describe what the attached photographs suggest. Say when you are unsure. A photograph shows an earlier moment. For an object, choose a close-up, uncertain, or absent.";
		request.Images.push_back(m_Photo.Image);
		if (subject == PhotoSubject::Object)
		{
			for (const PhotoRegion& region : m_Photo.Regions)
			{
				request.Images.push_back(region.Closeup);
				request.Candidates.push_back({region.Closeup.ID, "object in close-up image " + region.Closeup.ID});
			}
			request.Candidates.push_back({"absent", "none of the photographed objects matches the request"});
			request.Candidates.push_back({"uncertain", "something might match, but the photograph is unclear"});
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
		if (result->Status != DecisionStatus::Completed)
		{
			m_Receipt = result->Error;
			return true;
		}

		// 玩家可能已停止 Play 并重新进入同一场景。上一局的照片仍属于上一局的角色。
		const Ref<Scene> scene = m_Scene.lock();
		NpcSystem* npcs = scene ? scene->GetNpcs() : nullptr;
		const NpcDefinition* npc = npcs && npcs->SessionStart() == m_Session ? npcs->GetDefinition(m_Photo.Observer) : nullptr;
		if (!npc || !npc->Memory.Pictures)
		{
			m_Receipt = "本次观察已结束";
			return true;
		}

		if (!m_Verifying)
			m_Answer = m_Subject == PhotoSubject::Description ? result->Text : result->Choice;
		else if (result->Choice != "present")
		{
			m_Target = 0;
			m_Answer = result->Choice;
		}

		glm::vec3 direction = m_Photo.Forward;
		if (m_Subject == PhotoSubject::Object && !m_Verifying && m_Answer != "absent" && m_Answer != "uncertain")
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
			check.Candidates = {{"absent","the object sought is missing or different"},{"uncertain","the close-up is inconclusive"},{"present","this visible object appears to match"}};
			m_Verifying = true;
			m_Request = m_Client.Start(std::move(check));
			return false;
		}

		std::string evidence = m_Answer;
		if (m_Subject == PhotoSubject::Object)
		{
			evidence = "查找「" + m_SearchText + "」：";
			if (m_Answer == "absent") evidence += "当时的照片里没有辨认出来";
			else if (m_Answer == "uncertain") evidence += "看不清，需要换个位置再看";
			else
			{
				evidence += "照片的 " + m_Answer + " 区域看起来相似";
				const auto region = std::find_if(m_Photo.Regions.begin(), m_Photo.Regions.end(),
					[&](const PhotoRegion& r) { return r.Entity == m_Target; });
				if (region != m_Photo.Regions.end()) direction = region->Direction;
			}
		}
		// 照片只给出方向。沿地面往那个方向迈几步，留给角色换角度重看的机会。
		std::optional<glm::vec3> place;
		direction.y = 0.f;
		if (m_SuggestPlace && m_Answer != "absent" && npc->SearchStep > 0.f && glm::length(direction) > 0.f)
			place = m_Photo.Eye + glm::normalize(direction) * npc->SearchStep;
		m_Receipt = npcs->GetMemory(m_Photo.Observer)->RememberPhoto(m_Target, evidence, m_Photo.Seconds, m_Photo.Image.ID, place)
			? "已记录拍摄时的判断" : "角色当前不保留照片";
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
