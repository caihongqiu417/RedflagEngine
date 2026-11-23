#include "rfpch.h"
#include "ComponentReflection.h"
#include "Entity.h"

namespace Redflag::Reflection
{
	namespace
	{
		bool Finite(float value) { return std::isfinite(value); }

		bool Finite(glm::vec3 value)
		{
			return Finite(value.x) && Finite(value.y) && Finite(value.z);
		}

		bool Positive(float value) { return value > 0.f; }

		bool Positive(glm::vec3 value)
		{
			return glm::all(glm::greaterThan(value, glm::vec3(0.f)));
		}

		template<typename C, typename M>
		Field Bind(Field field, M C::* member, bool (*valid)(const C&), std::vector<M> enumerators = {})
		{
			// 成员指针使字段改名或改类型时在编译阶段就能发现。枚举名字与具体枚举值一一对应。
			const auto names = field.Choices;
			auto read = [member, names, enumerators](const C& owner) -> std::optional<Value> {
				if constexpr (std::is_enum_v<M>) {
					const auto found = std::find(enumerators.begin(), enumerators.end(), owner.*member);
					if (found == enumerators.end()) return {};
					return names[found - enumerators.begin()];
				} else return owner.*member;
			};

			field.Default = read(C{}).value();
			field.Read = [read](Entity entity) -> std::optional<Value> {
				if (!entity || !entity.HasComponent<C>()) return {};
				return read(entity.GetComponent<C>());
			};
			field.Write = [member, names, enumerators, valid](Entity entity, const Value& input) {
				if (!entity || !entity.HasComponent<C>()) return false;
				C& owner = entity.GetComponent<C>();
				C candidate = owner;
				if constexpr (std::is_enum_v<M>) {
					const auto* name = std::get_if<std::string>(&input);
					if (!name) return false;
					const auto found = std::find(names.begin(), names.end(), *name);
					if (found == names.end()) return false;
					candidate.*member = enumerators[found - names.begin()];
				} else {
					const auto* value = std::get_if<M>(&input);
					if (!value) return false;
					candidate.*member = *value;
				}
				// 半径和总长等字段有相互约束。整组值有效后才写回这个成员。
				if (!valid(candidate)) return false;
				owner.*member = candidate.*member;
				return true;
			};
			return field;
		}

		#include "ComponentFields.inc"
	}

	const std::vector<Component>& Types()
	{
		static const std::vector<Component> types = RegisterComponents();
		return types;
	}

	const Component* FindComponent(std::string_view name)
	{
		for (const Component& component : Types())
			if (component.Name == name) return &component;
		return nullptr;
	}

	const Field* Component::FindField(std::string_view name) const
	{
		for (const Field& field : Fields)
			if (field.Name == name) return &field;
		return nullptr;
	}
}
