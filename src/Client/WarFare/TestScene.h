#ifndef CLIENT_WARFARE_TESTSCENE_H
#define CLIENT_WARFARE_TESTSCENE_H

#pragma once

// Diagnostic scene for the RHI GL backend (docs/PORT_POSIX_PLAN.md,
// T6.6/T6.7 acceptance): drawn entirely through IRHIDevice, so it renders
// pixels on the GL backend and exercises the same call sequence headlessly on
// the Null backend. Covers the pre-transformed (XYZRHW) path, vertex colors
// (BGRA), matrices, texturing with a generated checkerboard, texture-stage
// combiners and per-vertex directional lighting.

struct IRHIDevice;

/// Draws one frame of the scene. fTime drives the animation; the width/height
/// are the window size in pixels (for the pre-transformed background quad).
void TestSceneTick(IRHIDevice* pDevice, float fTime, int iPixelWidth, int iPixelHeight);

/// Releases the scene's device resources. Call before destroying the device.
void TestSceneRelease();

#endif // CLIENT_WARFARE_TESTSCENE_H
