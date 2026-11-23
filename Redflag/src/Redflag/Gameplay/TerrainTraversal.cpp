#include "rfpch.h"
#include "TerrainTraversal.h"
#include "ColliderBounds.h"
#include "Redflag/Scene/Entity.h"

namespace Redflag
{
	namespace
	{
		// 这些距离按一名约 1.8 米高的站立角色选取。踏步、跳跃和攀爬分别使用不同的高度。
		constexpr float StepHeight = .30f, ClimbHeight = 1.65f, JumpReach = 1.85f;
		constexpr float Gravity = 9.81f, JumpSpeed = 4.7f;
		constexpr float JumpHeight = JumpSpeed * JumpSpeed / (2.f * Gravity);
		constexpr float WalkableY = .70710678f; // cos(45°)，再陡的坡不能作为落脚处。
		const glm::vec3 Up(0,1,0);

		std::optional<RayHit3D> Floor(Scene& scene, uint64_t actor, glm::vec3 position, float above, float below)
		{
			return scene.Raycast(position + Up * above, -Up, above + below, actor);
		}

		glm::vec3 AlongPath(const TerrainTraversal& motion, float fraction)
		{
			if (motion.Mode == TraversalMode::Climb) {
				// 先把身体提到平台高度，再从边缘向里移。半空中斜穿墙面会把膝盖送进墙里。
				const glm::vec3 raised(motion.Start.x, motion.Landing.y, motion.Start.z);
				constexpr float pull = .58f; // 总时长中用于上提身体的比例。
				return fraction < pull ? glm::mix(motion.Start, raised, fraction / pull)
					: glm::mix(raised, motion.Landing, (fraction - pull) / (1.f - pull));
			}
			const float time = fraction * motion.Duration;
			glm::vec3 point = glm::mix(motion.Start, motion.Landing, fraction);
			point.y = motion.Start.y + JumpSpeed * time - .5f * Gravity * time * time;
			return point;
		}

		bool BodyFits(Scene& scene, uint64_t actor, glm::vec3 from, glm::vec3 to, float radius, float height)
		{
			glm::vec3 direction = to - from;
			const float distance = glm::length(direction);
			if (distance < .0001f) return true;
			direction /= distance;
			// 在胸口和头顶各扫一个球。角色向墙边靠近时，头顶的横梁也必须能通过。
			const float bodyRadius = radius * .9f;
			for (float level : {height * .48f, height - bodyRadius}) {
				if (scene.SweepSphere(from + Up * level, direction, bodyRadius, distance, actor) < distance - .001f)
					return false;
			}
			return true;
		}

		bool Plan(Scene& scene, uint64_t actor, TerrainTraversal& motion, glm::vec3 start,
			const RayHit3D& floor, glm::vec3 ledge, float radius, float height)
		{
			const float rise = floor.Position.y + motion.Clearance - start.y;
			if (floor.Normal.y < WalkableY || rise > ClimbHeight || rise < -JumpHeight) return false;
			motion.Start = start;
			motion.Landing = floor.Position + Up * motion.Clearance;
			motion.Ledge = ledge;
			motion.Mode = rise > JumpHeight ? TraversalMode::Climb : TraversalMode::Jump;
			motion.Duration = motion.Mode == TraversalMode::Climb ? 1.f :
				(JumpSpeed + std::sqrt(std::max(0.f, JumpSpeed * JumpSpeed - 2.f * Gravity * rise))) / Gravity;
			motion.Clock = 0.f;

			// 检查整段起跳至落地的路线。每段最多占移动时间的十二分之一，能跟随抛物线弯曲。
			const glm::vec3 originAboveFeet = Up * motion.Clearance;
			glm::vec3 last = start;
			for (int i = 1; i <= 12; ++i) {
				glm::vec3 next = AlongPath(motion, float(i) / 12.f);
				if (!BodyFits(scene, actor, last - originAboveFeet, next - originAboveFeet, radius, height)) return false;
				last = next;
			}
			return true;
		}

		TraversalMode Advance(Scene& scene, uint64_t actor, TerrainTraversal& motion,
			glm::vec3& position, float seconds, float radius, float height)
		{
			motion.Clock = std::min(motion.Duration, motion.Clock + seconds);
			const glm::vec3 next = AlongPath(motion, motion.Clock / motion.Duration);
			const glm::vec3 originAboveFeet = Up * motion.Clearance;
			const auto landing = Floor(scene, actor, motion.Landing - originAboveFeet, StepHeight, StepHeight);
			if (!landing || landing->Normal.y < WalkableY ||
				std::abs(landing->Position.y + motion.Clearance - motion.Landing.y) > .04f ||
				!BodyFits(scene, actor, position - originAboveFeet, next - originAboveFeet, radius, height)) {
				CancelTraversal(scene, actor);
				return motion.Mode = TraversalMode::Blocked;
			}
			position = next;
			if (motion.Clock < motion.Duration) return motion.Mode;
			motion.SupportNormal = landing->Normal;
			return motion.Mode = TraversalMode::Ground;
		}

		void Slide(Scene& scene, uint64_t actor, glm::vec3& position, glm::vec3 delta, float radius)
		{
			// 没有地面碰撞体的旧场景保留自己的角色高度。沿两个轴尝试移动，仍可以贴着箱子走。
			for (int axis : {0,2}) {
				float distance = std::abs(delta[axis]);
				if (distance == 0.f) continue;
				glm::vec3 direction(0);
				direction[axis] = std::copysign(1.f, delta[axis]);
				const auto hit = scene.Raycast(position + Up * .35f, direction, distance + radius, actor);
				if (hit) distance = std::clamp(hit->Distance - radius, 0.f, distance);
				position += direction * distance;
			}
		}
	}

	// 线段若穿过设备的碰撞盒，比较沿左右边缘走的路，先去较近的拐角。
	glm::vec3 NextBoxCorner(Entity target, glm::vec3 start, glm::vec3 finish, float radius)
	{
		const float yaw = target.GetComponent<TransformComponent>().Rotation.y;
		const glm::quat facing = glm::angleAxis(yaw, Up);
		const auto bounds = GetColliderBounds(target, facing);
		if (!bounds) return finish;

		auto local = [&](glm::vec3 p) {
			const glm::vec3 v = glm::conjugate(facing) * (p - bounds->Center);
			return glm::vec2(v.x, v.z);
		};
		auto world = [&](glm::vec2 p) {
			const glm::vec3 v = bounds->Center + facing * glm::vec3(p.x, 0, p.y);
			return glm::vec3(v.x, start.y, v.z);
		};
		const glm::vec2 a = local(start), b = local(finish);
		// 扩出角色的半径，再留 3.5 厘米，经过边缘时鞋和障碍物有间隙。
		const glm::vec2 ext = glm::vec2(bounds->HalfSize.x,bounds->HalfSize.z) + radius + .035f;
		auto clear = [&](glm::vec2 p, glm::vec2 q) {
			float first = 0.f, last = 1.f;
			for (int i=0;i<2;++i) {
				const float d = q[i]-p[i], edge = ext[i]-.005f;
				if (std::abs(d)<.0001f) { if (std::abs(p[i])>=edge) return true; continue; }
				const float t0=(-edge-p[i])/d, t1=(edge-p[i])/d;
				first = std::max(first,std::min(t0,t1)); last = std::min(last,std::max(t0,t1));
			}
			return last<=first;
		};
		// 两个角色挤在一起时，中心可能已经落进扩大的外框。先朝最近的边离开。
		if (std::abs(a.x)<ext.x-.005f && std::abs(a.y)<ext.y-.005f) {
			const int axis=ext.x-std::abs(a.x) < ext.y-std::abs(a.y) ? 0 : 1;
			glm::vec2 edge=a;
			edge[axis]=std::copysign(ext[axis]+.025f,a[axis]);
			return world(edge);
		}
		if (clear(a,b) || (std::abs(b.x)<ext.x && std::abs(b.y)<ext.y)) return finish;
		const glm::vec2 corners[]={{-ext.x,-ext.y},{ext.x,-ext.y},{ext.x,ext.y},{-ext.x,ext.y}};
		float shortest = std::numeric_limits<float>::max();
		glm::vec2 next=b;
		for (int i=0;i<4;++i) {
			if (!clear(a,corners[i]) || glm::distance(a,corners[i])<.015f) continue;
			for (int sign : {-1,1}) {
				float length = glm::distance(a,corners[i]);
				for (int k=0;k<4;++k) {
					const int j=(i+sign*k+4)%4;
					if (clear(corners[j],b)) {
						length += glm::distance(corners[j],b);
						if (length<shortest) {shortest=length; next=corners[i];}
						break;
					}
					length += glm::distance(corners[j],corners[(j+sign+4)%4]);
				}
			}
		}
		return world(next);
	}

	TraversalMode MoveCharacter(Scene& scene, uint64_t actor, glm::vec3 delta, float radius, float seconds, TraversalInput input)
	{
		Entity entity = scene.GetEntityByUUID(actor);
		if (!entity) return TraversalMode::Blocked;
		auto& motion = scene.GetRegistry().get_or_emplace<TerrainTraversal>(static_cast<entt::entity>(entity));
		TransformComponent& transform = entity.GetComponent<TransformComponent>();
		glm::vec3& position = transform.Translation;
		const auto body = GetColliderBounds(entity);
		const float height = body ? 2.f * body->HalfSize.y : 1.8f;
		// 从碰撞体的最低处找地面。角色原点既可以在脚底，也可以在身体中间。
		if (body) motion.Clearance = body->HalfSize.y - (body->Center.y - position.y);
		if (motion.InAir()) return Advance(scene, actor, motion, position, seconds, radius, height);

		delta.y = 0.f;
		const float distance = glm::length(delta);
		const auto ground = Floor(scene, actor, position - Up * motion.Clearance, StepHeight, StepHeight * 2.f);
		if (!ground) {
			if (motion.HasSupport) return motion.Mode = TraversalMode::Blocked;
			Slide(scene, actor, position, delta, radius);
			return motion.Mode = TraversalMode::Ground;
		}
		if (!motion.HasSupport) {
			// 没有身体碰撞盒的模型沿用场景中摆好的高度。
			if (!body) motion.Clearance = std::max(0.f, position.y - ground->Position.y);
			motion.HasSupport = true;
		}
		motion.SupportNormal = ground->Normal;
		const glm::vec3 direction = distance > 0.f ? delta / distance : glm::vec3(0.f);
		std::optional<RayHit3D> wall;
		if (distance > 0.f) {
			const auto hit = scene.Raycast(ground->Position + Up * .07f, direction, radius + distance, actor);
			if (hit && hit->Normal.y < WalkableY) wall = hit;
		}
		const auto next = Floor(scene, actor, ground->Position + delta, StepHeight + .01f, StepHeight);
		glm::vec3 edge = wall ? wall->Position : ground->Position;
		std::optional<RayHit3D> top;
		if (wall) {
			top = Floor(scene, actor, edge + direction * (radius + .025f), ClimbHeight + .01f, StepHeight);
			if (top) edge.y = top->Position.y;
		}
		const bool lowStep = top && top->Normal.y >= WalkableY && top->Position.y - ground->Position.y <= StepHeight;
		if (input != TraversalInput::Jump && (!wall || lowStep) && next && next->Normal.y >= WalkableY) {
			glm::vec3 destination = next->Position + Up * motion.Clearance;
			if (!BodyFits(scene, actor, position - Up * motion.Clearance, next->Position, radius, height)) return motion.Mode = TraversalMode::Blocked;
			const float rise = std::abs(next->Position.y - ground->Position.y);
			position = destination;
			motion.SupportNormal = next->Normal;
			return motion.Mode = rise > .025f && next->Normal.y > .9999f ? TraversalMode::Step :
				next->Normal.y < .9999f ? TraversalMode::Slope : TraversalMode::Ground;
		}

		if (input != TraversalInput::Walk && seconds > 0.f) {
			if (wall && top) {
				if (Plan(scene, actor, motion, position, *top, edge, radius, height))
					return Advance(scene, actor, motion, position, seconds, radius, height);
			} else if (!wall) {
				// 从正常的一步开始往前找落脚处。遇到空沟时，要让脚掌的中心越过对岸边缘。
				const float preferred = input == TraversalInput::Jump ?
					std::min(JumpReach, distance / seconds * (2.f * JumpSpeed / Gravity)) : distance + radius;
				for (float ahead = preferred; ahead <= JumpReach + .001f; ahead += std::max(.12f, radius * .5f)) {
					const auto landing = Floor(scene, actor, ground->Position + direction * ahead, JumpHeight, JumpHeight);
					if (!landing || landing->Normal.y < WalkableY) continue;
					if (Plan(scene, actor, motion, position, *landing, edge, radius, height))
						return Advance(scene, actor, motion, position, seconds, radius, height);
					break;
				}
			}
		}
		if (input == TraversalInput::Walk && delta.x != 0.f && delta.z != 0.f) {
			// 斜向移动被墙挡住时，保留沿墙面走的那一部分。
			const TraversalMode x = MoveCharacter(scene, actor, {delta.x,0,0}, radius, seconds);
			const TraversalMode z = MoveCharacter(scene, actor, {0,0,delta.z}, radius, seconds);
			return motion.Mode = z == TraversalMode::Blocked ? x : z;
		}
		return motion.Mode = TraversalMode::Blocked;
	}

	void CancelTraversal(Scene& scene, uint64_t actor)
	{
		Entity entity = scene.GetEntityByUUID(actor);
		if (!entity) return;
		auto* motion = scene.GetRegistry().try_get<TerrainTraversal>(static_cast<entt::entity>(entity));
		if (!motion || !motion->InAir()) return;
		// 中止后回到这次起跳的落脚点。即使中途松开按钮，角色也不会停在半空。
		auto& position = entity.GetComponent<TransformComponent>().Translation;
		const auto support = Floor(scene, actor, motion->Start - Up * motion->Clearance, StepHeight, ClimbHeight);
		if (support) position = support->Position + Up * motion->Clearance;
		motion->Mode = TraversalMode::Ground;
		motion->Clock = 0.f;
	}
}
