#ifndef CLIENT_ASSETEXPLORER_ORBITCAMERA_H
#define CLIENT_ASSETEXPLORER_ORBITCAMERA_H

#pragma once

// Orbit camera for the Asset Explorer 3D viewport
// (docs/ASSET_EXPLORER_PLAN.md, M3). Pure math over the engine's left-handed,
// Y-up __Vector3/__Matrix44, so it is unit-testable without a GL context
// (tests/AssetExplorer/OrbitCamera_test.cpp). The camera orbits a target point
// on a sphere (yaw around Y, pitch elevation) at a given distance and produces
// the LookAtLH view and PerspectiveFovLH projection the RHI expects.

#include <MathUtils/Matrix44.h>
#include <MathUtils/Vector3.h>

namespace assetexplorer
{

class OrbitCamera
{
public:
	/// Frame an axis-aligned bounding box: centers the target and pulls the
	/// camera back far enough to fit the box, and sets sensible near/far planes.
	void FrameBounds(const __Vector3& vMin, const __Vector3& vMax);

	/// Orbit by the given yaw/pitch deltas (radians). Pitch is clamped to keep
	/// the up vector well-defined (never straight over the poles).
	void Orbit(float dYawRad, float dPitchRad);

	/// Dolly in/out by a multiplicative factor (>1 further, <1 closer). Distance
	/// is clamped to a sane range around the framed radius.
	void Dolly(float factor);

	__Vector3 Target() const { return m_target; }
	__Vector3 Eye() const;
	__Matrix44 View() const;
	__Matrix44 Projection(float aspect) const;

	// Accessors (also used by the tests).
	float Distance() const { return m_distance; }
	float Yaw() const { return m_yaw; }
	float Pitch() const { return m_pitch; }
	float NearPlane() const { return m_near; }
	float FarPlane() const { return m_far; }

private:
	__Vector3 m_target{0.0f, 0.0f, 0.0f};
	float m_distance = 5.0f;
	float m_radius   = 1.0f; // framed extent, for dolly clamping
	float m_yaw      = 0.7f; // radians, around +Y
	float m_pitch    = 0.35f; // radians, elevation
	float m_fovY     = 0.9f; // radians (~51 deg)
	float m_near     = 0.1f;
	float m_far      = 1000.0f;
};

} // namespace assetexplorer

#endif // CLIENT_ASSETEXPLORER_ORBITCAMERA_H
