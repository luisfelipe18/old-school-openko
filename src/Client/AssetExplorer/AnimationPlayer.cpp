#include "AnimationPlayer.h"

#include <algorithm>
#include <cmath>

namespace assetexplorer
{

void AnimationPlayer::SetClip(float startFrame, float endFrame, float framesPerSecond)
{
	m_start = startFrame;
	m_end   = std::max(endFrame, startFrame);
	m_fps   = framesPerSecond > 0.0f ? framesPerSecond : 30.0f;
	m_cur   = m_start;
}

void AnimationPlayer::SetSpeed(float speed)
{
	m_speed = std::clamp(speed, 0.05f, 10.0f);
}

void AnimationPlayer::Scrub(float frame)
{
	m_cur     = std::clamp(frame, m_start, m_end);
	m_playing = false;
}

void AnimationPlayer::Update(float dtSeconds)
{
	const float length = m_end - m_start;
	if (!m_playing || length <= 0.0f || dtSeconds <= 0.0f)
		return;

	m_cur += dtSeconds * m_fps * m_speed;

	if (m_cur >= m_end)
	{
		if (m_loop)
			m_cur = m_start + std::fmod(m_cur - m_start, length);
		else
		{
			m_cur     = m_end;
			m_playing = false;
		}
	}
}

float AnimationPlayer::Normalized() const
{
	const float length = m_end - m_start;
	if (length <= 0.0f)
		return 0.0f;
	return (m_cur - m_start) / length;
}

} // namespace assetexplorer
