#include <gtest/gtest.h>

#include <N3Base/RHI/RHIDeviceNull.h>

TEST(RHIDeviceNullTest, RenderStatesRoundTrip)
{
	RHIDeviceNull device;

	EXPECT_EQ(device.SetRenderState(D3DRS_FOGENABLE, TRUE), D3D_OK);
	EXPECT_EQ(device.SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA), D3D_OK);

	DWORD value = 0xDEAD;
	EXPECT_EQ(device.GetRenderState(D3DRS_FOGENABLE, &value), D3D_OK);
	EXPECT_EQ(value, static_cast<DWORD>(TRUE));

	EXPECT_EQ(device.GetRenderState(D3DRS_SRCBLEND, &value), D3D_OK);
	EXPECT_EQ(value, static_cast<DWORD>(D3DBLEND_SRCALPHA));

	// Unset states read back as 0 rather than failing (matches how the
	// engine probes state it hasn't touched).
	EXPECT_EQ(device.GetRenderState(D3DRS_LIGHTING, &value), D3D_OK);
	EXPECT_EQ(value, 0u);

	EXPECT_NE(device.GetRenderState(D3DRS_LIGHTING, nullptr), D3D_OK);
}

TEST(RHIDeviceNullTest, StageAndSamplerStatesArePerIndex)
{
	RHIDeviceNull device;

	device.SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	device.SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device.SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

	DWORD value = 0;
	device.GetTextureStageState(0, D3DTSS_COLOROP, &value);
	EXPECT_EQ(value, static_cast<DWORD>(D3DTOP_MODULATE));
	device.GetTextureStageState(1, D3DTSS_COLOROP, &value);
	EXPECT_EQ(value, static_cast<DWORD>(D3DTOP_DISABLE));
	device.GetSamplerState(0, D3DSAMP_MAGFILTER, &value);
	EXPECT_EQ(value, static_cast<DWORD>(D3DTEXF_LINEAR));
}

TEST(RHIDeviceNullTest, TransformsRoundTripAndDefaultToIdentity)
{
	RHIDeviceNull device;

	__Matrix44 world = __Matrix44::GetIdentity();
	world.m[3][0]    = 12.5f;
	world.m[3][2]    = -3.0f;

	EXPECT_EQ(device.SetTransform(D3DTS_WORLD, world.toD3D()), D3D_OK);

	__Matrix44 readBack;
	EXPECT_EQ(device.GetTransform(D3DTS_WORLD, readBack.toD3D()), D3D_OK);
	EXPECT_EQ(readBack.m[3][0], 12.5f);
	EXPECT_EQ(readBack.m[3][2], -3.0f);

	__Matrix44 untouched;
	EXPECT_EQ(device.GetTransform(D3DTS_VIEW, untouched.toD3D()), D3D_OK);
	EXPECT_EQ(untouched.m[0][0], 1.0f);
	EXPECT_EQ(untouched.m[3][0], 0.0f);
}

TEST(RHIDeviceNullTest, DrawsAndPresentsAreCounted)
{
	RHIDeviceNull device;

	const float vertices[9] = {};
	EXPECT_EQ(device.BeginScene(), D3D_OK);
	EXPECT_EQ(device.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, 12), D3D_OK);
	EXPECT_EQ(device.DrawPrimitive(D3DPT_LINELIST, 0, 1), D3D_OK);
	EXPECT_EQ(device.EndScene(), D3D_OK);
	EXPECT_EQ(device.Present(), D3D_OK);

	EXPECT_EQ(device.DrawCallCount(), 2);
	EXPECT_EQ(device.PresentCount(), 1);

	// Invalid UP draws are rejected and not counted.
	EXPECT_NE(device.DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, nullptr, 12), D3D_OK);
	EXPECT_EQ(device.DrawCallCount(), 2);
}

TEST(RHIDeviceNullTest, LightsRoundTrip)
{
	RHIDeviceNull device;

	_D3DLIGHT9 light   = {};
	light.Type         = D3DLIGHT_POINT;
	light.Range        = 42.0f;
	light.Position.x   = 1.0f;

	EXPECT_EQ(device.SetLight(3, &light), D3D_OK);
	EXPECT_EQ(device.LightEnable(3, TRUE), D3D_OK);

	_D3DLIGHT9 readBack = {};
	EXPECT_EQ(device.GetLight(3, &readBack), D3D_OK);
	EXPECT_EQ(readBack.Range, 42.0f);
	EXPECT_EQ(readBack.Position.x, 1.0f);

	BOOL enabled = FALSE;
	EXPECT_EQ(device.GetLightEnable(3, &enabled), D3D_OK);
	EXPECT_TRUE(enabled);
	EXPECT_EQ(device.GetLightEnable(7, &enabled), D3D_OK);
	EXPECT_FALSE(enabled);
}
