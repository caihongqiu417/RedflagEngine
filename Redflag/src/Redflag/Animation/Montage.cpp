#include "rfpch.h"
#include "Montage.h"
#include "Redflag/Core/UUID.h"
#include <yaml-cpp/yaml.h>

namespace Redflag
{
	float MontageBlend::Weight(float t) const
	{
		return AnimationTransition{{}, {}, Seconds, Curve}.Weight(t);
	}

	const MontageSection* AnimationMontage::Find(const std::string& name) const
	{
		for (const MontageSection& section : Sections)
			if (section.Name == name) return &section;
		return nullptr;
	}

	bool AnimationMontage::Validate(std::string& error) const
	{
		auto fail = [&](const std::string& detail) { error = detail; return false; };
		if (Name.empty() || Clip.empty() || Slot.empty() || Sections.empty()) return fail("请填写名字、原片、Slot 和至少一个播放段。");
		for (const MontageBlend* blend : {&BlendIn, &BlendOut})
			if (!(std::isfinite(blend->Seconds) && blend->Seconds >= 0 && blend->Curve.x >= 0 && blend->Curve.x <= 1 &&
				blend->Curve.y >= 0 && blend->Curve.y <= 1)) return fail("混合时间须为非负秒数，控制点须在 0 到 1 之间。");
		std::set<std::string> names;
		for (const std::string& bone : Bones)
			if (bone.empty() || !names.insert(bone).second) return fail("遮罩有空骨骼或重复骨骼。");

		auto range = [](float a, float b) { return std::isfinite(a) && std::isfinite(b) && a >= 0 && b > a; };
		names.clear();
		float duration = 0.f;
		for (const MontageSection& section : Sections)
		{
			if (section.Name.empty() || !names.insert(section.Name).second || !range(section.Start, section.End) ||
				(!section.Next.empty() && !Find(section.Next))) return fail("段名称、时间或下一段无效：" + section.Name);
			for (const MontageSection& other : Sections)
				if (&other != &section && other.Start < section.End && section.Start < other.End) return fail("播放段时间重叠：" + section.Name);
			duration = std::max(duration, section.End);
		}
		names.clear();
		for (const AnimationMarker& mark : Events)
			if (mark.Name.empty() || !names.insert(mark.Name).second || !std::isfinite(mark.Start) || mark.Start < 0 ||
				mark.End.value_or(mark.Start) > duration || (mark.End && !range(mark.Start, *mark.End))) return fail("通知名称或时间无效：" + mark.Name);
		for (const MontageWarp& warp : WarpWindows)
		{
			if (warp.Target.empty() || warp.Root.empty() || !range(warp.Start,warp.End) || warp.End > duration) return fail("对齐目标、根骨或窗口时间无效。");
			for (const MontageWarp& other : WarpWindows)
				if (&other != &warp && other.Start < warp.End && warp.Start < other.End) return fail("根骨移动的时间窗口重叠。");
		}
		error.clear();
		return true;
	}

	// 按默认播放路线计算经过的秒数。循环段只走一遍，跳过的段不贡献时间。
	static float RouteTime(const AnimationMontage& asset, const AnimationMarker* event)
	{
		float elapsed = 0;
		std::set<const MontageSection*> visited;
		const MontageSection* section = asset.Sections.empty() ? nullptr : &asset.Sections.front();
		while (section && visited.insert(section).second) {
			if (event && event->Start >= section->Start && event->Start <= section->End)
				return elapsed + event->Start - section->Start;
			elapsed += section->End - section->Start;
			section = asset.Find(section->Next);
		}
		return event ? -1.f : elapsed;
	}

	float AnimationMontage::Duration() const { return RouteTime(*this, nullptr); }

	std::optional<float> AnimationMontage::EventTime(const std::string& name) const
	{
		for (const AnimationMarker& mark : Events)
			if (mark.Name == name) {
				const float seconds = RouteTime(*this,&mark);
				if (seconds >= 0) return seconds;
			}
		return {};
	}

	//========== 蒙太奇资产读写 ==========

	static void CheckKeys(const YAML::Node& data, std::initializer_list<const char*> known)
	{
		if (!data.IsMap()) throw std::runtime_error("蒙太奇条目需要一组字段。");
		for (const YAML::const_iterator::value_type& row : data) {
			const std::string field = row.first.as<std::string>();
			if (std::find(known.begin(), known.end(), field) == known.end())
				throw std::runtime_error("当前版本无法读取字段 " + field + "，原文件会保留。");
		}
	}

	bool AnimationMontage::Load(const std::filesystem::path& file, std::string& error)
	{
		try
		{
			std::ifstream stream(file);
			if (!stream) throw std::runtime_error("无法打开蒙太奇 " + file.u8string());
			const YAML::Node data = YAML::Load(stream);
			CheckKeys(data, {"Name","Clip","Slot","Bones","BlendIn","BlendOut","Sections","Events","WarpWindows"});
			AnimationMontage value;
			value.Name = data["Name"].as<std::string>();
			value.Clip = data["Clip"].as<std::string>();
			value.Slot = data["Slot"].as<std::string>();
			if (data["Bones"]) value.Bones = data["Bones"].as<std::vector<std::string>>();
			auto blend = [&](const char* key, MontageBlend& b) {
				const YAML::Node node = data[key];
				if (!node) return;
				CheckKeys(node, {"Seconds","Curve"});
				b.Seconds = node["Seconds"].as<float>();
				if (const YAML::Node curve = node["Curve"]) {
					if (!curve.IsSequence() || curve.size() != 2) throw std::runtime_error("Curve 需要两个控制点。");
					b.Curve = {curve[0].as<float>(), curve[1].as<float>()};
				}
			};
			blend("BlendIn", value.BlendIn);
			blend("BlendOut", value.BlendOut);
			for (const YAML::Node& node : data["Sections"]) {
				CheckKeys(node, {"Name","Start","End","Next"});
				value.Sections.push_back({node["Name"].as<std::string>(),node["Start"].as<float>(),node["End"].as<float>(),node["Next"].as<std::string>("")});
			}
			for (const YAML::Node& node : data["Events"]) {
				CheckKeys(node, {"Name","At","Start","End"});
				if (node["At"] && (node["Start"] || node["End"])) throw std::runtime_error("同一条通知只能填写 At 或 Start/End。");
				AnimationMarker mark;
				mark.Name = node["Name"].as<std::string>();
				mark.Start = (node["At"] ? node["At"] : node["Start"]).as<float>();
				if (node["End"]) mark.End = node["End"].as<float>();
				value.Events.push_back(std::move(mark));
			}
			for (const YAML::Node& node : data["WarpWindows"]) {
				CheckKeys(node, {"Target","Root","Start","End"});
				value.WarpWindows.push_back({node["Target"].as<std::string>(),node["Root"].as<std::string>(),node["Start"].as<float>(),node["End"].as<float>()});
			}
			if (!value.Validate(error)) return false;
			*this = std::move(value);
			return true;
		}
		catch (const std::exception& exception) { error = exception.what(); return false; }
	}

	bool AnimationMontage::Save(const std::filesystem::path& file, std::string& error) const
	{
		if (!Validate(error)) return false;
		AnimationMontage saved;
		if (std::filesystem::is_regular_file(file) && !saved.Load(file,error)) return false;
		YAML::Node data;
		data["Name"] = Name;
		data["Clip"] = Clip;
		data["Slot"] = Slot;
		data["Bones"] = Bones;
		auto blend = [&](const char* key, const MontageBlend& b) {
			data[key]["Seconds"] = b.Seconds;
			data[key]["Curve"] = std::vector<float>{b.Curve.x,b.Curve.y};
		};
		blend("BlendIn", BlendIn);
		blend("BlendOut", BlendOut);
		for (const MontageSection& section : Sections) {
			YAML::Node node;
			node["Name"] = section.Name; node["Start"] = section.Start; node["End"] = section.End;
			if (!section.Next.empty()) node["Next"] = section.Next;
			data["Sections"].push_back(node);
		}
		for (const AnimationMarker& mark : Events) {
			YAML::Node node;
			node["Name"] = mark.Name; node[mark.End ? "Start" : "At"] = mark.Start;
			if (mark.End) node["End"] = *mark.End;
			data["Events"].push_back(node);
		}
		for (const MontageWarp& warp : WarpWindows) {
			YAML::Node node;
			node["Target"] = warp.Target; node["Root"] = warp.Root; node["Start"] = warp.Start; node["End"] = warp.End;
			data["WarpWindows"].push_back(node);
		}
		std::filesystem::path temporary = file;
		temporary += "." + std::to_string(uint64_t(UUID())) + ".tmp";
		std::ofstream stream(temporary);
		stream << data << '\n';
		stream.close();
		std::error_code cause;
		if (stream) std::filesystem::rename(temporary, file, cause);
		if (!stream || cause) {
			error = "蒙太奇写入失败：" + file.u8string() + " " + cause.message();
			std::filesystem::remove(temporary,cause);
			return false;
		}
		return true;
	}
}
