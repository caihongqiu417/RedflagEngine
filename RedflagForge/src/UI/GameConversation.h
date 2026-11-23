#pragma once

#include <imgui.h>
#include <string>

namespace Redflag
{
	class Entity;
	class NpcSystem;
	class VoicePanel;
	struct NpcEvent;

	/* 关卡可在 HUD 布局里调整对白字幕。关闭字幕后，语音仍照常播放。 */
	struct ConversationStyle
	{
		bool Subtitles = true;
		ImU32 Speaker = IM_COL32(255, 218, 154, 255);
		ImU32 Text = IM_COL32(255, 252, 244, 255);
		float Font = 28.f, BottomMargin = 174.f; // 1920 × 1080 布局中的像素。
		float ReadingRate = 7.f; // 没有语音时，每秒读过的字数。
	};

	/* 播放中的游戏视图持有玩家草稿，只用角色实际说出口的台词绘制字幕。 */
	class GameConversation
	{
	public:
		void Receive(const NpcEvent& event);
		void Draw(Entity actor, NpcSystem& game, VoicePanel& voice, const ConversationStyle& style,
			ImVec2 corner, ImVec2 size);
	private:
		bool m_Open = false, m_FocusInput = false, m_FocusGame = false;
		char m_Draft[1024]{};
		std::string m_Reply, m_Status;
		double m_SilentAt = -1, m_StatusAt = 0, m_LastAudioAt = -1, m_ReplyAt = 0;
	};
}
