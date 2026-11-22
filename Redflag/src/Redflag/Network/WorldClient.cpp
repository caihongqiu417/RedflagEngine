#include "rfpch.h"
#include "WorldClient.h"

#include <cmath>
#include <cstring>
#include <winsock.h>
#pragma comment(lib, "ws2_32.lib")

namespace Redflag
{
	namespace
	{
		// REDFServer 的消息头为四字节正文长度、两字节消息类型，两者均按大端发送。
		enum Message : uint16_t { Heartbeat = 6, Join = 32, Ready = 33, Publish = 34, State = 35, Ack = 36 };
		constexpr size_t MaxBody = 64 * 1024;
		using Bytes = std::vector<uint8_t>;
		using Clock = std::chrono::steady_clock;

		void Append32(Bytes& bytes, uint32_t value)
		{
			bytes.push_back(uint8_t(value >> 24));
			bytes.push_back(uint8_t(value >> 16));
			bytes.push_back(uint8_t(value >> 8));
			bytes.push_back(uint8_t(value));
		}

		uint32_t Read32(const uint8_t* p)
		{
			return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
		}

		uint16_t Read16(const uint8_t* p) { return uint16_t((uint16_t(p[0]) << 8) | p[1]); }

		void AppendVector(Bytes& bytes, const glm::vec3& value)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				uint32_t bits;
				std::memcpy(&bits, &value[axis], sizeof(bits));
				Append32(bytes, bits);
			}
		}

		bool ReadVector(const uint8_t* data, glm::vec3& value)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				uint32_t bits = Read32(data + axis * 4);
				std::memcpy(&value[axis], &bits, sizeof(bits));
				if (!std::isfinite(value[axis]))
					return false;
			}
			return true;
		}
	}

	struct WorldClient::Connection
	{
		SOCKET Socket = INVALID_SOCKET;
		bool Started = false, Joined = false;
		WorldRole Role = WorldRole::Viewer;
		Bytes Input, Pending;
		std::vector<WorldPose> Poses;
		uint32_t Revision = 0, Accepted = 0;
		Clock::time_point Seen, HeartbeatSent, FirstSent, LastSent;

		~Connection()
		{
			if (Socket != INVALID_SOCKET)
				closesocket(Socket);
			if (Started)
				WSACleanup();
		}

		bool Send(Message type, const Bytes& body)
		{
			Bytes frame;
			Append32(frame, uint32_t(body.size()));
			frame.push_back(uint8_t(type >> 8));
			frame.push_back(uint8_t(type));
			frame.insert(frame.end(), body.begin(), body.end());
			size_t offset = 0;
			while (offset < frame.size())
			{
				int written = send(Socket, reinterpret_cast<const char*>(frame.data() + offset), int(frame.size() - offset), 0);
				if (written <= 0)
					return false;
				offset += size_t(written);
			}
			return true;
		}

		bool Handle(uint16_t type, const uint8_t* data, size_t length)
		{
			if (!Joined)
				return Joined = type == Message::Ready && length == 0;
			if (type == Message::Heartbeat)
				return length == 0;
			if (Role == WorldRole::Host && type == Message::Ack && length == 4)
			{
				uint32_t sequence = Read32(data);
				if (!Pending.empty() && sequence == Read32(Pending.data()))
				{
					Accepted = sequence;
					Pending.clear();
				}
				return sequence <= Accepted;
			}
			if (Role != WorldRole::Viewer || type != Message::State || length < 6 || length != 6u + 32u * Read16(data + 4))
				return false;

			uint32_t version = Read32(data);
			if (version > Revision)
			{
				std::vector<WorldPose> snapshot;
				std::set<uint64_t> ids;
				for (size_t i = 6; i < length; i += 32)
				{
					WorldPose pose;
					pose.ID = (uint64_t(Read32(data + i)) << 32) | Read32(data + i + 4);
					if (!ids.insert(pose.ID).second || !ReadVector(data + i + 8, pose.Translation) || !ReadVector(data + i + 20, pose.Rotation))
						return false;
					snapshot.push_back(pose);
				}
				Poses = std::move(snapshot);
				Revision = version;
			}
			Bytes ack;
			Append32(ack, version);
			return Send(Message::Ack, ack);
		}

		bool Receive()
		{
			// select 只检查此刻是否有数据，让没有网络消息的一帧直接继续绘制。
			for (;;)
			{
				fd_set reads;
				FD_ZERO(&reads);
				FD_SET(Socket, &reads);
				timeval wait{};
				int readable = select(0, &reads, nullptr, nullptr, &wait);
				if (readable <= 0)
					return readable == 0;
				char block[8192];
				int count = recv(Socket, block, sizeof(block), 0);
				if (count <= 0)
					return false;
				Input.insert(Input.end(), block, block + count);

				size_t used = 0;
				while (Input.size() - used >= 6)
				{
					const uint8_t* head = Input.data() + used;
					uint32_t length = Read32(head);
					if (length > MaxBody)
						return false;
					if (Input.size() - used < 6u + length)
						break;
					if (!Handle(Read16(head + 4), head + 6, length))
						return false;
					Seen = Clock::now();
					used += 6 + length;
				}
				Input.erase(Input.begin(), Input.begin() + used);
			}
		}

		bool Tick()
		{
			if (!Receive())
				return false;
			const Clock::time_point now = Clock::now();
			if (now - Seen >= std::chrono::seconds(10))
				return false;
			if (Joined && now - HeartbeatSent >= std::chrono::seconds(1))
			{
				HeartbeatSent = now;
				if (!Send(Message::Heartbeat, {}))
					return false;
			}
			if (Pending.empty())
				return true;
			if (now - FirstSent >= std::chrono::seconds(10))
				return false;
			if (now - LastSent < std::chrono::milliseconds(500))
				return true;
			LastSent = now;
			return Send(Message::Publish, Pending);
		}
	};

	WorldClient::WorldClient() = default;
	WorldClient::~WorldClient() = default;

	bool WorldClient::Connect(const std::string& ipv4, uint16_t port, WorldRole role)
	{
		Disconnect();
		std::unique_ptr<Connection> connection = std::make_unique<Connection>();
		WSADATA data;
		if (WSAStartup(MAKEWORD(2, 2), &data))
			return false;
		connection->Started = true;
		connection->Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in server{};
		server.sin_family = AF_INET;
		server.sin_port = htons(port);
		server.sin_addr.s_addr = inet_addr(ipv4.c_str());
		if (connection->Socket == INVALID_SOCKET || server.sin_addr.s_addr == INADDR_NONE ||
			connect(connection->Socket, reinterpret_cast<sockaddr*>(&server), sizeof(server)) != 0)
			return false;

		DWORD timeoutMs = 500;
		setsockopt(connection->Socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeoutMs), sizeof(timeoutMs));
		connection->Role = role;
		connection->Seen = connection->HeartbeatSent = Clock::now();
		if (!connection->Send(Message::Join, { uint8_t(role) }))
			return false;
		m_Connection = std::move(connection);
		return true;
	}

	void WorldClient::Disconnect() { m_Connection.reset(); }
	bool WorldClient::Ready() const { return m_Connection && m_Connection->Joined; }

	bool WorldClient::Update()
	{
		if (m_Connection && m_Connection->Tick())
			return true;
		Disconnect();
		return false;
	}

	bool WorldClient::Publish(const std::vector<WorldPose>& poses)
	{
		if (!Ready() || m_Connection->Role != WorldRole::Host || !m_Connection->Pending.empty() ||
			poses.size() > (MaxBody - 6) / 32 || m_Connection->Accepted == UINT32_MAX)
			return false;
		Bytes& body = m_Connection->Pending;
		Append32(body, m_Connection->Accepted + 1);
		body.push_back(uint8_t(poses.size() >> 8));
		body.push_back(uint8_t(poses.size()));
		for (const WorldPose& pose : poses)
		{
			Append32(body, uint32_t(pose.ID >> 32));
			Append32(body, uint32_t(pose.ID));
			AppendVector(body, pose.Translation);
			AppendVector(body, pose.Rotation);
		}
		m_Connection->FirstSent = m_Connection->LastSent = Clock::now();
		if (m_Connection->Send(Message::Publish, body))
			return true;
		Disconnect();
		return false;
	}

	uint32_t WorldClient::LastAcceptedPublish() const { return m_Connection ? m_Connection->Accepted : 0; }
	uint32_t WorldClient::Revision() const { return m_Connection ? m_Connection->Revision : 0; }

	const std::vector<WorldPose>& WorldClient::Poses() const
	{
		static const std::vector<WorldPose> empty;
		return m_Connection ? m_Connection->Poses : empty;
	}
}
