#include "OrbitCamera.h"

#include <algorithm>
#include <cmath>

namespace assetexplorer
{
namespace
{
constexpr float kPitchLimit = 1.5f; // ~86 deg, keeps the up vector well-defined
}

void OrbitCamera::FrameBounds(const __Vector3& vMin, const __Vector3& vMax)
{
	const __Vector3 center = (vMin + vMax) * 0.5f;
	const __Vector3 extent = vMax - center;
	float radius = extent.Magnitude();
	if (radius < 1e-4f)
		radius = 1.0f;

	m_target   = center;
	m_radius   = radius;
	// Distance so the bounding sphere fits the vertical FOV, with a small margin.
	m_distance = radius / std::sin(m_fovY * 0.5f) * 1.25f;
	m_near     = std::max(m_distance * 0.02f, 0.01f);
	m_far      = m_distance + radius * 8.0f;
}

void OrbitCamera::Orbit(float dYawRad, float dPitchRad)
{
	m_yaw += dYawRad;
	m_pitch = std::clamp(m_pitch + dPitchRad, -kPitchLimit, kPitchLimit);
}

void OrbitCamera::Dolly(float factor)
{
	m_distance = std::clamp(m_distance * factor, m_radius * 0.15f, m_radius * 40.0f);
}

__Vector3 OrbitCamera::Eye() const
{
	const float cp = std::cos(m_pitch);
	const float sp = std::sin(m_pitch);
	const __Vector3 offset(cp * std::sin(m_yaw), sp, cp * std::cos(m_yaw));
	return m_target + offset * m_distance;
}

__Matrix44 OrbitCamera::View() const
{
	__Matrix44 v;
	v.LookAtLH(Eye(), m_target, __Vector3(0.0f, 1.0f, 0.0f));
	return v;
}

__Matrix44 OrbitCamera::Projection(float aspect) const
{
	__Matrix44 p;
	p.PerspectiveFovLH(m_fovY, aspect > 0.0f ? aspect : 1.0f, m_near, m_far);
	return p;
}

} // namespace assetexplorer
