#pragma once

#include "Redflag/AI/SpeechPlayback.h"

namespace Redflag
{
	/* 在试玩中记录这名角色实际送往播放设备的语音。普通播放返回空函数。 */
	SpeechPlayback::Observer ObserveGameSpeech(uint64_t speaker);
}
