#include "rfpch.h"
#include "FieldInspector.h"
#include "Redflag/Scene/ComponentReflection.h"
#include "Redflag/Scene/Entity.h"
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <yaml-cpp/yaml.h>

namespace Redflag
{
	static std::filesystem::path NotesPath(std::filesystem::path scene)
	{
		return scene.empty() ? scene : scene.replace_extension(".tuning.yaml");
	}

	void FieldInspector::Open(const std::filesystem::path& scene)
	{
		m_Path = NotesPath(scene);
		m_Notes.clear();
		if (m_Path.empty() || !std::filesystem::exists(m_Path)) return;

		try
		{
			const YAML::Node document = YAML::LoadFile(m_Path.string());
			for (const YAML::Node& item : document["notes"])
				m_Notes.push_back({ item["entities"].as<std::vector<uint64_t>>(), item["component"].as<std::string>(),
					item["field"].as<std::string>(), item["reason"].as<std::string>(), item["script"].as<std::string>(""),
					item["evidence"].as<std::vector<std::string>>(std::vector<std::string>{}) });
		}
		catch (const YAML::Exception& error)
		{
			m_Notes.clear();
			RF_CORE_WARN("Could not read field notes in {0}: {1}", m_Path.u8string(), error.what());
		}
	}

	void FieldInspector::SaveAs(const std::filesystem::path& scene)
	{
		const std::filesystem::path next = NotesPath(scene);
		if (next != m_Path && !m_Path.empty() && std::filesystem::exists(m_Path))
		{
			// 另存场景时带上原记录。目标场景已有的手工说明优先保留。
			std::error_code error;
			std::filesystem::copy_file(m_Path, next, std::filesystem::copy_options::skip_existing, error);
			if (error) RF_CORE_WARN("Could not copy field notes: {0}", error.message());
		}
		Open(scene);
	}

	void FieldInspector::DrawNote(uint64_t entity, const std::string& component, const std::string& field,
		const std::string& script) const
	{
		for (const Note& note : m_Notes)
		{
			if (note.Component != component || note.Field != field || (!note.Script.empty() && note.Script != script) ||
				std::find(note.Entities.begin(), note.Entities.end(), entity) == note.Entities.end()) continue;

			ImGui::PushID(field.c_str());
			if (ImGui::TreeNodeEx("取值记录", ImGuiTreeNodeFlags_SpanAvailWidth))
			{
				ImGui::TextWrapped("%s", note.Reason.c_str());
				if (!note.Evidence.empty() && ImGui::TreeNode("查看依据"))
				{
					for (const std::string& path : note.Evidence)
					{
						ImGui::TextWrapped("%s", path.c_str());
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("点击复制位置");
						if (ImGui::IsItemClicked()) ImGui::SetClipboardText(path.c_str());
					}
					ImGui::TreePop();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}

	static const char* ChoiceLabel(const std::string& value)
	{
		if (value == "Static") return "静态";
		if (value == "Dynamic") return "动态";
		if (value == "Kinematic") return "运动学";
		return value.c_str();
	}

	static bool EditChoice(const std::string& label, const Reflection::Field& field, std::string& value)
	{
		bool changed = false;
		if (ImGui::BeginCombo(label.c_str(), ChoiceLabel(value)))
		{
			for (const std::string& choice : field.Choices)
			{
				if (ImGui::Selectable(ChoiceLabel(choice), choice == value))
				{
					value = choice;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	void FieldInspector::Draw(Entity entity, const char* name) const
	{
		const Reflection::Component& type = *Reflection::FindComponent(name);
		for (const Reflection::Field& field : type.Fields)
		{
			if (field.Name == "Density" && type.Name == "RigidBody3DComponent" &&
				entity.GetComponent<RigidBody3DComponent>().Type != BodyType3D::Dynamic) continue;

			std::optional<Reflection::Value> value = field.Read(entity);
			if (!value) continue;
			const bool fullSize = type.Name == "BoxCollider3DComponent" && field.Name == "HalfSize";
			std::string label = fullSize ? "尺寸 XYZ" : field.Label;
			if (!field.Unit.empty()) label += "（" + field.Unit + "）";

			bool changed = false;
			if (field.Kind == Reflection::FieldKind::Float)
				changed = ImGui::DragFloat(label.c_str(), &std::get<float>(*value), .01f);
			else if (field.Kind == Reflection::FieldKind::Vector3)
			{
				// 编辑器展示盒子的完整长宽高，场景沿用从中心到表面的半尺寸。
				const float scale = fullSize ? 2.f : 1.f;
				glm::vec3 displayed = std::get<glm::vec3>(*value) * scale;
				changed = ImGui::DragFloat3(label.c_str(), glm::value_ptr(displayed), .01f);
				*value = displayed / scale;
			}
			else
				changed = EditChoice(label, field, std::get<std::string>(*value));

			const bool rejected = changed && !field.Write(entity, *value);
			if (rejected || ImGui::IsItemHovered())
				ImGui::SetTooltip("%s%s%s", field.Help.c_str(), field.Constraint.empty() ? "" : "\n", field.Constraint.c_str());
			DrawNote(entity.GetUUID(), type.Name, field.Name);
		}
	}
}
