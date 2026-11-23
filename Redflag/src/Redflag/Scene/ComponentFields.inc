// 由 scripts/generate_component_fields.py 生成。修改 ComponentFields.json 后重新运行。
static_assert(std::is_same_v<decltype(CapsuleCollider3DComponent::Center), glm::vec3>);
static_assert(std::is_same_v<decltype(CapsuleCollider3DComponent::Radius), float>);
static_assert(std::is_same_v<decltype(CapsuleCollider3DComponent::Height), float>);
bool Valid(const CapsuleCollider3DComponent& c)
{
	return Finite(c.Center) &&
		(c.Axis == decltype(c.Axis)::X || c.Axis == decltype(c.Axis)::Y || c.Axis == decltype(c.Axis)::Z) &&
		Finite(c.Radius) &&
		Positive(c.Radius) &&
		Finite(c.Height) &&
		Positive(c.Height) &&
		c.Height >= 2.f * c.Radius;
}

static_assert(std::is_same_v<decltype(BoxCollider3DComponent::Center), glm::vec3>);
static_assert(std::is_same_v<decltype(BoxCollider3DComponent::HalfSize), glm::vec3>);
bool Valid(const BoxCollider3DComponent& c)
{
	return Finite(c.Center) &&
		Finite(c.HalfSize) &&
		Positive(c.HalfSize);
}

static_assert(std::is_same_v<decltype(RigidBody3DComponent::Density), float>);
bool Valid(const RigidBody3DComponent& c)
{
	return (c.Type == decltype(c.Type)::Static || c.Type == decltype(c.Type)::Dynamic || c.Type == decltype(c.Type)::Kinematic) &&
		Finite(c.Density) &&
		Positive(c.Density);
}
std::vector<Component> RegisterComponents()
{
	return {
		{"CapsuleCollider3DComponent", "3D 胶囊碰撞体", {
			Bind({"Center", "中心偏移", "米", "从模型原点量到胶囊中心。调整 Y 可以让碰撞体的底部对齐角色的鞋底。", "", FieldKind::Vector3, {}}, &CapsuleCollider3DComponent::Center, Valid, {}),
			Bind({"Axis", "长轴", "", "选择胶囊沿模型的哪个方向放置。站立角色一般沿 Y 轴，躺下时可沿身体长度放置。", "", FieldKind::Enum, {"X", "Y", "Z"}}, &CapsuleCollider3DComponent::Axis, Valid, {decltype(CapsuleCollider3DComponent::Axis)::X, decltype(CapsuleCollider3DComponent::Axis)::Y, decltype(CapsuleCollider3DComponent::Axis)::Z}),
			Bind({"Radius", "半径", "米", "从胶囊中轴量到外沿。调大后身体更宽，走过同一条窄道时需要更大的空隙。", "半径必须大于零，且不能超过当前总长的一半。", FieldKind::Float, {}}, &CapsuleCollider3DComponent::Radius, Valid, {}),
			Bind({"Height", "总长", "米", "沿长轴从一个圆头量到另一个圆头，包含两端的半球。总长等于直径时，胶囊变成球。", "总长至少等于直径。", FieldKind::Float, {}}, &CapsuleCollider3DComponent::Height, Valid, {})
		}, [](Entity e) { return e && e.HasComponent<CapsuleCollider3DComponent>() && Valid(e.GetComponent<CapsuleCollider3DComponent>()); }},
		{"BoxCollider3DComponent", "3D 盒形碰撞体", {
			Bind({"Center", "中心偏移", "米", "模型原点到盒子中心的偏移，沿模型自己的三个轴填写。", "", FieldKind::Vector3, {}}, &BoxCollider3DComponent::Center, Valid, {}),
			Bind({"HalfSize", "半尺寸", "米", "场景文件记录从中心到各面的距离。属性面板中的尺寸 XYZ 是这里的两倍。", "三个方向的半尺寸都必须大于零。", FieldKind::Vector3, {}}, &BoxCollider3DComponent::HalfSize, Valid, {})
		}, [](Entity e) { return e && e.HasComponent<BoxCollider3DComponent>() && Valid(e.GetComponent<BoxCollider3DComponent>()); }},
		{"RigidBody3DComponent", "3D 刚体", {
			Bind({"BodyType", "类型", "", "地面通常选静态。动态物体会在重力作用下落；运动学物体由游戏设置位置，物理系统按这个位置检查碰撞。", "", FieldKind::Enum, {"Static", "Dynamic", "Kinematic"}}, &RigidBody3DComponent::Type, Valid, {decltype(RigidBody3DComponent::Type)::Static, decltype(RigidBody3DComponent::Type)::Dynamic, decltype(RigidBody3DComponent::Type)::Kinematic}),
			Bind({"Density", "密度", "千克/立方米", "物理系统用密度乘以碰撞形状的体积，求出动态刚体的质量。同样大小的箱子，密度越大就越重。", "密度必须大于零。", FieldKind::Float, {}}, &RigidBody3DComponent::Density, Valid, {})
		}, [](Entity e) { return e && e.HasComponent<RigidBody3DComponent>() && Valid(e.GetComponent<RigidBody3DComponent>()); }}
	};
}
