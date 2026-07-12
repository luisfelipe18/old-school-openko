#include <gtest/gtest.h>

#include <FlyCamera.h>

#include <cmath>

using namespace assetexplorer;

TEST(FlyCamera, ForwardIsPlusZAtIdentity)
{
	FlyCamera cam;
	cam.SetLook(0.0f, 0.0f);
	const __Vector3 f = cam.Forward();
	EXPECT_NEAR(f.x, 0.0f, 1e-5f);
	EXPECT_NEAR(f.y, 0.0f, 1e-5f);
	EXPECT_NEAR(f.z, 1.0f, 1e-5f);
	// right is +X for a +Z forward, Y-up, left-handed frame.
	const __Vector3 r = cam.Right();
	EXPECT_NEAR(r.x, 1.0f, 1e-5f);
	EXPECT_NEAR(r.y, 0.0f, 1e-5f);
	EXPECT_NEAR(r.z, 0.0f, 1e-5f);
}

TEST(FlyCamera, MoveForwardFollowsViewDirection)
{
	FlyCamera cam;
	cam.SetPosition(__Vector3(0, 0, 0));
	cam.SetLook(0.0f, 0.0f);
	cam.MoveForward(5.0f);
	EXPECT_NEAR(cam.Position().z, 5.0f, 1e-4f);
	EXPECT_NEAR(cam.Position().x, 0.0f, 1e-4f);

	// Face +X (yaw 90 deg) and move forward.
	cam.SetPosition(__Vector3(0, 0, 0));
	cam.SetLook(1.5708f, 0.0f);
	cam.MoveForward(3.0f);
	EXPECT_NEAR(cam.Position().x, 3.0f, 1e-3f);
	EXPECT_NEAR(cam.Position().z, 0.0f, 1e-3f);
}

TEST(FlyCamera, MoveRightIsHorizontalAndPerpendicular)
{
	FlyCamera cam;
	cam.SetPosition(__Vector3(0, 10, 0));
	cam.SetLook(0.0f, -0.5f); // pitched down; right must stay horizontal
	cam.MoveRight(4.0f);
	EXPECT_NEAR(cam.Position().x, 4.0f, 1e-3f);
	EXPECT_NEAR(cam.Position().y, 10.0f, 1e-4f); // no vertical drift
}

TEST(FlyCamera, MoveUpUsesWorldUp)
{
	FlyCamera cam;
	cam.SetPosition(__Vector3(1, 2, 3));
	cam.SetLook(0.7f, -0.6f);
	cam.MoveUp(5.0f);
	EXPECT_NEAR(cam.Position().y, 7.0f, 1e-4f);
	EXPECT_NEAR(cam.Position().x, 1.0f, 1e-4f);
	EXPECT_NEAR(cam.Position().z, 3.0f, 1e-4f);
}

TEST(FlyCamera, LookClampsPitch)
{
	FlyCamera cam;
	cam.Look(0.0f, 100.0f);
	EXPECT_LT(cam.Pitch(), 1.5708f);
	cam.Look(0.0f, -200.0f);
	EXPECT_GT(cam.Pitch(), -1.5708f);
	// yaw accumulates freely
	cam.SetLook(0.0f, 0.0f);
	cam.Look(2.0f, 0.0f);
	EXPECT_NEAR(cam.Yaw(), 2.0f, 1e-5f);
}

TEST(FlyCamera, ForwardIsUnitLength)
{
	FlyCamera cam;
	cam.SetLook(1.1f, 0.4f);
	EXPECT_NEAR(cam.Forward().Magnitude(), 1.0f, 1e-5f);
}
