#include "rfpch.h"
#include "AnimationClip.h"
#include <fstream>
#include <stdexcept>

namespace Redflag
{
	namespace
	{
		void Expect(std::istream& file, const char* expected)
		{
			std::string word;
			if (!(file >> word) || word != expected)
				throw std::runtime_error(std::string("BVH expected ") + expected);
		}

		struct Channel { size_t Joint; int Axis; bool Rotation; };

		void ReadJoint(std::istream& file, int parent, float scale, Skeleton& rig, std::vector<Channel>& channels)
		{
			Joint joint{};
			joint.Parent = parent;
			file >> joint.Name;
			Expect(file, "{");
			Expect(file, "OFFSET");
			file >> joint.Offset.x >> joint.Offset.y >> joint.Offset.z;
			joint.Offset *= scale;
			size_t index = rig.Joints.size();
			rig.Joints.push_back(joint);

			Expect(file, "CHANNELS");
			int count;
			if (!(file >> count) || count < 0 || count > 6)
				throw std::runtime_error("Invalid BVH channel count");
			for (int i = 0; i < count; ++i)
			{
				std::string name;
				file >> name;
				if (name.empty() || name[0] < 'X' || name[0] > 'Z' ||
					(name.substr(1) != "position" && name.substr(1) != "rotation"))
					throw std::runtime_error("Unknown BVH channel: " + name);
				channels.push_back({index, name[0]-'X', name.substr(1) == "rotation"});
			}

			std::string word;
			while (file >> word && word != "}")
			{
				if (word == "JOINT")
					ReadJoint(file, static_cast<int>(index), scale, rig, channels);
				else if (word == "End")
				{
					Expect(file, "Site");
					Expect(file, "{");
					Expect(file, "OFFSET");
					glm::vec3 tip;
					file >> tip.x >> tip.y >> tip.z;
					Expect(file, "}");
				}
				else
					throw std::runtime_error("Unexpected BVH field: " + word);
			}
		}
	}

	AnimationClip AnimationClip::LoadBVH(const std::filesystem::path& path, float metersPerUnit)
	{
		std::ifstream file(path);
		if (!file)
			throw std::runtime_error("Cannot open BVH: " + path.string());
		AnimationClip clip;
		std::vector<Channel> channels;
		Expect(file, "HIERARCHY");
		Expect(file, "ROOT");
		ReadJoint(file, -1, metersPerUnit, clip.Rig, channels);
		Expect(file, "MOTION");
		Expect(file, "Frames:");
		int count;
		if (!(file >> count) || count <= 0)
			throw std::runtime_error("BVH contains no frames");
		Expect(file, "Frame");
		Expect(file, "Time:");
		if (!(file >> clip.FrameTime) || !std::isfinite(clip.FrameTime) || clip.FrameTime <= 0.0f)
			throw std::runtime_error("Invalid BVH frame interval");

		Pose rest = clip.Rig.RestPose();
		for (int f = 0; f < count; ++f)
		{
			Pose pose = rest;
			for (const Channel& channel : channels)
			{
				float value;
				if (!(file >> value) || !std::isfinite(value))
					throw std::runtime_error("Incomplete or invalid BVH frame " + std::to_string(f));
				JointPose& joint = pose[channel.Joint];
				if (channel.Rotation)
				{
					glm::vec3 axis(0.0f);
					axis[channel.Axis] = 1.0f;
					joint.Rotation = joint.Rotation * glm::angleAxis(glm::radians(value), axis);
				}
				else
				{
					glm::vec3 translation(0);
					translation[channel.Axis] = value * metersPerUnit;
					joint.Position += joint.Rotation * translation;
				}
			}
			clip.Frames.push_back(std::move(pose));
		}
		return clip;
	}

	Pose AnimationClip::Sample(float seconds) const
	{
		float cycle = std::fmod(seconds, Duration());
		if (cycle < 0.0f)
			cycle += Duration();
		float frame = cycle / FrameTime;
		size_t first = static_cast<size_t>(frame) % Frames.size();
		const Pose& next = Frames[(first + 1) % Frames.size()];
		Pose pose = Frames[first];
		float blend = frame - std::floor(frame);
		for (size_t i = 0; i < pose.size(); ++i)
		{
			pose[i].Position = glm::mix(pose[i].Position, next[i].Position, blend);
			pose[i].Rotation = glm::slerp(pose[i].Rotation, next[i].Rotation, blend);
		}
		return pose;
	}
}
