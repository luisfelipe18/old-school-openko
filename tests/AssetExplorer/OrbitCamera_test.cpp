#include <gtest/gtest.h>

#include <OrbitCamera.h>

#include <cmath>

using namespace assetexplorer;

namespace
{
float Dist(const __Vector3& a, const __Vector3& b)
{
	return (a - b).Magnitude();
}
} // namespace

TEST(OrbitCamera, FrameBoundsCentersTargetAndBacksOff)
{
	OrbitCamera cam;
	cam.FrameBounds(__Vector3(-1, -1, -1), __Vector3(1, 1, 1));

	// Target is the box center.
	EXPECT_NEAR(cam.Target().x, 0.0f, 1e-4f);
	EXPECT_NEAR(cam.Target().y, 0.0f, 1e-4f);
	EXPECT_NEAR(cam.Target().z, 0.0f, 1e-4f);

	// The eye sits a positive distance from the target, and the box (radius
	// sqrt(3)) fits, so the distance is at least the radius.
	EXPECT_GT(cam.Distance(), std::sqrt(3.0f));
	EXPECT_NEAR(Dist(cam.Eye(), cam.Target()), cam.Distance(), 1e-3f);

	// Near/far bracket the model.
	EXPECT_GT(cam.NearPlane(), 0.0f);
	EXPECT_GT(cam.FarPlane(), cam.Distance());
}

TEST(OrbitCamera, FrameBoundsOffCenterBox)
{
	OrbitCamera cam;
	cam.FrameBounds(__Vector3(9, -1, -1), __Vector3(11, 1, 1));
	EXPECT_NEAR(cam.Target().x, 10.0f, 1e-4f);
	EXPECT_NEAR(Dist(cam.Eye(), cam.Target()), cam.Distance(), 1e-3f);
}

TEST(OrbitCamera, DegenerateBoundsStillUsable)
{
	OrbitCamera cam;
	cam.FrameBounds(__Vector3(0, 0, 0), __Vector3(0, 0, 0)); // a point
	EXPECT_GT(cam.Distance(), 0.0f);
	EXPECT_GT(cam.FarPlane(), cam.NearPlane());
}

TEST(OrbitCamera, OrbitChangesYawAndClampsPitch)
{
	OrbitCamera cam;
	const float yaw0 = cam.Yaw();
	cam.Orbit(1.0f, 0.0f);
	EXPECT_NEAR(cam.Yaw(), yaw0 + 1.0f, 1e-5f);

	// Pitch clamps well before the pole in both directions.
	cam.Orbit(0.0f, 100.0f);
	EXPECT_LT(cam.Pitch(), 1.5708f);
	EXPECT_GT(cam.Pitch(), 0.0f);
	cam.Orbit(0.0f, -100.0f);
	EXPECT_GT(cam.Pitch(), -1.5708f);
}

TEST(OrbitCamera, DollyScalesDistanceWithinClamp)
{
	OrbitCamera cam;
	cam.FrameBounds(__Vector3(-1, -1, -1), __Vector3(1, 1, 1));
	const float d0 = cam.Distance();
	cam.Dolly(0.5f);
	EXPECT_LT(cam.Distance(), d0);

	// Dollying way in clamps to a positive minimum, not zero/negative.
	for (int i = 0; i < 50; ++i)
		cam.Dolly(0.5f);
	EXPECT_GT(cam.Distance(), 0.0f);

	// Dollying way out clamps to a finite maximum.
	for (int i = 0; i < 100; ++i)
		cam.Dolly(2.0f);
	EXPECT_LT(cam.Distance(), 1e6f);
}

TEST(OrbitCamera, EyeMovesWhenOrbiting)
{
	OrbitCamera cam;
	cam.FrameBounds(__Vector3(-1, -1, -1), __Vector3(1, 1, 1));
	const __Vector3 e0 = cam.Eye();
	cam.Orbit(1.0f, 0.0f);
	const __Vector3 e1 = cam.Eye();
	EXPECT_GT(Dist(e0, e1), 0.1f);
	// Orbiting keeps the same radius from the target.
	EXPECT_NEAR(Dist(e1, cam.Target()), cam.Distance(), 1e-3f);
}
