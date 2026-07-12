#ifndef CLIENT_ASSETEXPLORER_FLYCAMERA_H
#define CLIENT_ASSETEXPLORER_FLYCAMERA_H

#pragma once

// Free-fly camera for the Asset Explorer terrain viewport
// (docs/ASSET_EXPLORER_PLAN.md, M6). The engine streams terrain patches around
// the camera's position, so a map is roamed rather than orbited: WASD/QE move
// the eye and the mouse looks around. Pure math over the engine's left-handed,
// Y-up __Vector3/__Matrix44, unit-tested without a GL context
// (tests/AssetExplorer/FlyCamera_test.cpp).

#include <MathUtils/Matrix44.h>
#include <MathUtils/Vector3.h>

namespace assetexplorer
{

class FlyCamera
{
public:
	void SetPosition(const __Vector3& pos) { m_pos = pos; }
	void SetLook(float yawRad, float pitchRad);

	__Vector3 Position() const { return m_pos; }
	__Vector3 Forward() const;
	__Vector3 Right() const;

	/// Look by yaw/pitch deltas (radians); pitch is clamped away from the poles.
	void Look(float dYawRad, float dPitchRad);

	void MoveForward(float distance); // along the view direction
	void MoveRight(float distance);   // along the (horizontal) right vector
	void MoveUp(float distance);      // along world up

	__Matrix44 View() const;
	__Matrix44 Projection(float aspect, float fovY, float nearPlane, float farPlane) const;

	float Yaw() const { return m_yaw; }
	float Pitch() const { return m_pitch; }

private:
	__Vector3 m_pos{0.0f, 0.0f, 0.0f};
	float m_yaw   = 0.0f;
	float m_pitch = 0.0f;
};

} // namespace assetexplorer

#endif // CLIENT_ASSETEXPLORER_FLYCAMERA_H
