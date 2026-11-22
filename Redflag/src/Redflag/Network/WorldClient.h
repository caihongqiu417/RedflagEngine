#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <glm/vec3.hpp>

namespace Redflag
{
	struct WorldPose
	{
		uint64_t ID;
		glm::vec3 Translation;
		glm::vec3 Rotation; // 与 TransformComponent 相同，三个轴均为弧度。
	};

	enum class WorldRole : uint8_t { Viewer, Host };

	/* 每帧调用 Update 收取服务器消息。旁观者只展示服务器已接受的姿态。 */
	class WorldClient
	{
	public:
		WorldClient();
		~WorldClient();

		bool Connect(const std::string& ipv4, uint16_t port, WorldRole role);
		void Disconnect();
		bool Update();
		bool Ready() const;

		// 主机每次发送完整实体列表。上一份还没有确认时返回 false，下帧可发最新位置。
		bool Publish(const std::vector<WorldPose>& poses);
		uint32_t LastAcceptedPublish() const;

		uint32_t Revision() const;
		const std::vector<WorldPose>& Poses() const;

	private:
		struct Connection;
		std::unique_ptr<Connection> m_Connection;
	};
}
