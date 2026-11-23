#include "rfpch.h"
#include "GameConversation.h"
#include "../VoicePanel.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/Scene/Entity.h"
#include <string_view>

namespace Redflag
{
	void GameConversation::Receive(const NpcEvent& event)
	{
		if (event.Kind == "玩家" || event.Kind == "中止")
		{
			m_Reply.clear();
			m_Status.clear();
			if (event.Kind == "玩家" && m_Open)
			{
				m_Open = false;
				m_FocusGame = true;
			}
		}
		if (!event.Speech.empty())
		{
			m_Reply = event.Speech;
			m_ReplyAt = ImGui::GetTime();
			m_SilentAt = -1;
			m_LastAudioAt = -1;
			m_Status.clear();
		}
		else if (event.Kind == "模型")
		{
			m_Status = "暂时没有收到回复，按 T 可以再试一次。";
			m_StatusAt = ImGui::GetTime();
		}
	}

	//========== 画面下方的对白字幕 ==========

	static void DrawCentered(std::string_view text, float x, float y, float fontSize, ImU32 color, float opacity = 1.f)
	{
		ImFont* font = ImGui::GetFont();
		const char* begin = text.data();
		const char* end = begin + text.size();
		const float width = font->CalcTextSizeA(fontSize, FLT_MAX, 0, begin, end).x;
		const ImVec2 pos{x - width * .5f, y};
		const float edge = std::max(1.f, fontSize / 16.f);
		ImDrawList* draw = ImGui::GetWindowDrawList();

		// 在明亮的天空和地面上，用细描边衬出文字；四周仍然能看到场景。
		color = (color & ~IM_COL32_A_MASK) | (ImU32((color >> IM_COL32_A_SHIFT) * opacity) << IM_COL32_A_SHIFT);
		for (const ImVec2 offset : {ImVec2{-edge,0}, ImVec2{edge,0}, ImVec2{0,-edge}, ImVec2{0,edge}})
			draw->AddText(font, fontSize, {pos.x + offset.x, pos.y + offset.y}, IM_COL32(23,28,32,int(225 * opacity)), begin, end);
		draw->AddText(font, fontSize, pos, color, begin, end);
	}

	static void DrawSubtitle(const std::string& name, const std::string& speech, const ConversationStyle& style,
		const SpeechPlayback& audio, double& silentAt, double& lastAudioAt, ImVec2 corner, ImVec2 size)
	{
		const double now = ImGui::GetTime();
		constexpr double fadeSeconds = .25; // 台词结束后用四分之一秒淡出，保持末字可读。
		float opacity = 1.f;
		float progress = audio.PlaybackProgress();
		if (progress >= 0.f)
		{
			if (audio.FirstAudioMs() < 0) return;
			lastAudioAt = now;
		}
		else if (lastAudioAt >= 0)
		{
			if (now - lastAudioAt >= fadeSeconds) return;
			opacity = float(1.0 - (now - lastAudioAt) / fadeSeconds);
			progress = 1.f;
		}
		else
		{
			// 合成和播放由后台完成。只有确认语音失败后，才开始计算无声字幕的阅读时间。
			if (audio.Error().empty()) return;
			if (silentAt < 0) silentAt = now;
			const auto letters = std::count_if(speech.begin(), speech.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; });
			const double duration = std::max(2.0, double(letters) / std::max(1.f, style.ReadingRate));
			const double elapsed = now - silentAt;
			if (elapsed >= duration) return;
			opacity = float(std::min(1.0, (duration - elapsed) / fadeSeconds));
			progress = float(elapsed / duration);
		}

		const float scale = std::min(size.x / 1920.f, size.y / 1080.f);
		const float fontSize = std::max(14.f, style.Font * scale);
		const float wrap = std::max(fontSize * 2.f, size.x * .56f);
		const char* begin = speech.data();
		const char* end = begin + speech.size();
		const char* spoken = begin + std::min(size_t(progress * speech.size()), speech.size() - 1);
		std::string_view page[2];
		int count = 0;

		// 每页最多两行。声音没有逐字时间戳，用这句在声卡中的播放进度选择字幕页。
		while (begin < end)
		{
			const char* paragraph = std::find(begin, end, '\n');
			const char* line = ImGui::GetFont()->CalcWordWrapPositionA(fontSize / ImGui::GetFont()->FontSize, begin, paragraph, wrap);
			while (line < paragraph && (static_cast<unsigned char>(*line) & 0xc0) == 0x80) ++line;
			if (line > begin) page[count++] = {begin, size_t(line - begin)};
			begin = line;
			while (begin < end && (*begin == ' ' || *begin == '\r' || *begin == '\n')) ++begin;
			if (count == 2)
			{
				if (spoken < begin || begin == end) break;
				count = 0;
			}
		}
		if (!count) return;

		const float center = corner.x + size.x * .5f;
		float y = corner.y + size.y - style.BottomMargin * scale - count * fontSize;
		DrawCentered(name, center, y - fontSize, fontSize * .78f, style.Speaker, opacity);
		for (int i = 0; i < count; ++i, y += fontSize)
			DrawCentered(page[i], center, y, fontSize, style.Text, opacity);
	}

	//========== 文字与语音输入 ==========

	void GameConversation::Draw(Entity actor, NpcSystem& game, VoicePanel& voice, const ConversationStyle& style,
		ImVec2 corner, ImVec2 size)
	{
		if (m_FocusGame)
		{
			ImGui::SetWindowFocus();
			m_FocusGame = false;
		}
		const bool opening = !m_Open && ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput &&
			ImGui::IsKeyPressed(ImGuiKey_T, false);
		if (opening) m_Open = m_FocusInput = true;
		if (m_Open && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			m_Open = false;
			voice.CancelRecording();
			ImGui::SetWindowFocus();
		}

		// 字幕只画文字，不创建窗口，玩家在听对白时仍能走动和转动镜头。
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(corner, {corner.x + size.x, corner.y + size.y}, true);
		if (style.Subtitles && !m_Reply.empty())
		{
			DrawSubtitle(actor.GetName(), m_Reply, style, voice.Playback(), m_SilentAt, m_LastAudioAt, corner, size);
			// 首次载入音色可能较慢。等了两秒才提示，正常接话时画面不会闪一行字。
			if (!m_Open && voice.Playback().FirstAudioMs() < 0 && voice.Playback().Error().empty()
				&& ImGui::GetTime() - m_ReplyAt >= 2.)
				DrawCentered("正在准备" + actor.GetName() + "的语音…", corner.x + size.x * .5f,
					corner.y + size.y * .93f, ImGui::GetFontSize(), style.Text);
		}
		if (!m_Open && !m_Status.empty() && ImGui::GetTime() - m_StatusAt < 4.0)
			DrawCentered(m_Status, corner.x + size.x * .5f, corner.y + size.y * .93f, ImGui::GetFontSize(), style.Text);
		draw->PopClipRect();
		if (!m_Open) return;

		const float em = ImGui::GetFontSize();
		const float width = std::min(size.x * .94f, em * 28.f);
		ImGui::SetNextWindowPos({corner.x + size.x * .5f, corner.y + size.y - em * .7f}, ImGuiCond_Always, {.5f, 1.f});
		ImGui::SetNextWindowSize({width, 0});
		if (opening) ImGui::SetNextWindowFocus();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {em * .5f, em * .4f});
		ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(48,61,68,242));
		constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
		if (ImGui::Begin("##GameConversation", nullptr, flags))
		{
			const float buttonWidth = ImGui::CalcTextSize("按住说话").x + ImGui::GetStyle().FramePadding.x * 2;
			ImGui::SetNextItemWidth(-buttonWidth - ImGui::GetStyle().ItemSpacing.x);
			if (m_FocusInput)
			{
				ImGui::SetKeyboardFocusHere();
				m_FocusInput = false;
			}
			const std::string hint = "对" + actor.GetName() + "说…  Enter 发送";
			if (ImGui::InputTextWithHint("##Message", hint.c_str(), m_Draft, sizeof(m_Draft), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				if (game.Say(actor.GetUUID(), m_Draft))
				{
					m_Draft[0] = 0;
					m_Open = false;
				}
				else m_FocusInput = true;
			}
			ImGui::SameLine();
			voice.DrawCapture();
			if (!m_Status.empty()) ImGui::TextWrapped("%s", m_Status.c_str());
		}
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
		if (!m_Open) ImGui::SetWindowFocus();
	}
}
