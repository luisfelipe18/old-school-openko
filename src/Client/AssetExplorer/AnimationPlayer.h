#ifndef CLIENT_ASSETEXPLORER_ANIMATIONPLAYER_H
#define CLIENT_ASSETEXPLORER_ANIMATIONPLAYER_H

#pragma once

// Animation timeline for the Asset Explorer character viewport
// (docs/ASSET_EXPLORER_PLAN.md, M4). Owns the play/pause/loop/scrub state over
// a clip's frame range and produces the current frame the character is posed to
// (CN3Chr::Tick(frame)). Pure logic, no engine dependency, so the play/advance/
// loop/clamp behaviour is unit-tested (tests/AssetExplorer/AnimationPlayer_test.cpp).

namespace assetexplorer
{

class AnimationPlayer
{
public:
	/// Set the active clip's frame range and native rate. Resets the playhead to
	/// the start. A zero-length range is tolerated (Frame() stays at start).
	void SetClip(float startFrame, float endFrame, float framesPerSecond);

	void Play() { m_playing = true; }
	void Pause() { m_playing = false; }
	void TogglePlay() { m_playing = !m_playing; }
	bool IsPlaying() const { return m_playing; }

	void SetLooping(bool loop) { m_loop = loop; }
	bool IsLooping() const { return m_loop; }

	/// Playback speed multiplier (1 = native rate). Clamped to a sane range.
	void SetSpeed(float speed);
	float Speed() const { return m_speed; }

	/// Move the playhead to an explicit frame (clamped to the clip) and pause.
	void Scrub(float frame);

	/// Advance the playhead by dt seconds when playing; loops or stops at the end.
	void Update(float dtSeconds);

	float Frame() const { return m_cur; }
	float StartFrame() const { return m_start; }
	float EndFrame() const { return m_end; }
	float FramesPerSecond() const { return m_fps; }

	/// Position within the clip in [0,1] (0 for a zero-length clip).
	float Normalized() const;

private:
	float m_start   = 0.0f;
	float m_end     = 0.0f;
	float m_fps     = 30.0f;
	float m_cur     = 0.0f;
	float m_speed   = 1.0f;
	bool m_playing  = false;
	bool m_loop     = true;
};

} // namespace assetexplorer

#endif // CLIENT_ASSETEXPLORER_ANIMATIONPLAYER_H
