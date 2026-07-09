#ifndef N3BASE_RHI_RHITEXTURES_H
#define N3BASE_RHI_RHITEXTURES_H

#pragma once

// RHI textures (docs/PORT_POSIX_PLAN.md, task T6.2).
//
// Same shape as IDirect3DTexture9 (per-level LockRect/UnlockRect/GetLevelDesc)
// so N3Texture migrates textually. Ownership follows the D3D9 COM contract:
// Release() drops a reference and returns the new count; the object (and its
// GPU/system storage) is destroyed when the count reaches zero.

#include "../My_3DStruct.h"

struct IRHITexture
{
	virtual HRESULT LockRect(
		UINT level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD flags) = 0;
	virtual HRESULT UnlockRect(UINT level)                                        = 0;
	virtual HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC* pDesc)              = 0;
	virtual UINT GetLevelCount() const                                           = 0;

	/// Drops a reference; returns the new count. Destroys at zero.
	virtual ULONG Release()                                                       = 0;

protected:
	virtual ~IRHITexture() = default; // destroy through Release()
};

#endif // N3BASE_RHI_RHITEXTURES_H
