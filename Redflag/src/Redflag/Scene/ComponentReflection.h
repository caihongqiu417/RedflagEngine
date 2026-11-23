#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <glm/vec3.hpp>

namespace Redflag { class Entity; }

namespace Redflag::Reflection
{
	enum class FieldKind { Float, Vector3, Enum };
	using Value = std::variant<float, glm::vec3, std::string>;

	/* Name 是场景文件里的字段名。枚举用同一份文件中的名字读写，避免把枚举序号写入资产。 */
	struct Field
	{
		std::string Name, Label, Unit, Help, Constraint;
		FieldKind Kind;
		std::vector<std::string> Choices;
		Value Default;
		std::function<std::optional<Value>(Entity)> Read;
		// 写入前检查完整组件。类型不符、字段越界或实体没有该组件时返回 false。
		std::function<bool(Entity, const Value&)> Write;
	};

	struct Component
	{
		std::string Name, Label;
		std::vector<Field> Fields;
		std::function<bool(Entity)> IsValid;
		const Field* FindField(std::string_view name) const;
	};

	const std::vector<Component>& Types();
	const Component* FindComponent(std::string_view name);
}
