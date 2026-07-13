#include "FlyCamera.h"

#include <algorithm>
#include <cmath>

namespace assetexplorer
{
namespace
{
constexpr float kPitchLimit = 1.5f; // ~86 deg
}

void FlyCamera::SetLook(float yawRad, float pitchRad)
{
	m_yaw   = yawRad;
	m_pitch = std::clamp(pitchRad, -kPitchLimit, kPitchLimit);
}

__Vector3 FlyCamera::Forward() const
{
	const float cp = std::cos(m_pitch);
	// yaw around +Y, pitch elevation; at yaw=pitch=0 this looks down +Z (LH).
	return __Vector3(std::sin(m_yaw) * cp, std::sin(m_pitch), std::cos(m_yaw) * cp);
}

__Vector3 FlyCamera::Right() const
{
	// right = up x forward (left-handed), flattened to the horizontal plane.
	__Vector3 r;
	r.Cross(__Vector3(0.0f, 1.0f, 0.0f), Forward());
	r.y = 0.0f;
	if (r.Magnitude() > 1e-5f)
		r.Normalize();
	return r;
}

void FlyCamera::Look(float dYawRad, float dPitchRad)
{
	m_yaw += dYawRad;
	m_pitch = std::clamp(m_pitch + dPitchRad, -kPitchLimit, kPitchLimit);
}

void FlyCamera::MoveForward(float distance)
{
	m_pos += Forward() * distance;
}

void FlyCamera::MoveRight(float distance)
{
	m_pos += Right() * distance;
}

void FlyCamera::MoveUp(float distance)
{
	m_pos += __Vector3(0.0f, 1.0f, 0.0f) * distance;
}

__Matrix44 FlyCamera::View() const
{
	__Matrix44 v;
	v.LookAtLH(m_pos, m_pos + Forward(), __Vector3(0.0f, 1.0f, 0.0f));
	return v;
}

__Matrix44 FlyCamera::Projection(float aspect, float fovY, float nearPlane, float farPlane) const
{
	__Matrix44 p;
	p.PerspectiveFovLH(fovY, aspect > 0.0f ? aspect : 1.0f, nearPlane, farPlane);
	return p;
}

} // namespace assetexplorer
