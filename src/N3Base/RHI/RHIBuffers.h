#ifndef N3BASE_RHI_RHIBUFFERS_H
#define N3BASE_RHI_RHIBUFFERS_H

#pragma once

// RHI geometry buffers (docs/PORT_POSIX_PLAN.md, task T6.1).
//
// Same shape as IDirect3DVertexBuffer9/IDirect3DIndexBuffer9 so call sites
// migrate textually; ownership is single-owner: Release() destroys the
// object (and the GPU resource in hardware backends) and always returns 0.

#include "../My_3DStruct.h"

struct IRHIVertexBuffer
{
	virtual HRESULT Lock(UINT offsetToLock, UINT sizeToLock, void** ppData, DWORD flags) = 0;
	virtual HRESULT Unlock()                                                             = 0;

	/// Destroys the buffer. Do not use the pointer afterwards.
	virtual ULONG Release()                                                              = 0;

	virtual UINT Length() const                                                          = 0;
	virtual DWORD FVF() const                                                            = 0;

protected:
	virtual ~IRHIVertexBuffer() = default; // destroy through Release()
};

struct IRHIIndexBuffer
{
	virtual HRESULT Lock(UINT offsetToLock, UINT sizeToLock, void** ppData, DWORD flags) = 0;
	virtual HRESULT Unlock()                                                             = 0;

	/// Destroys the buffer. Do not use the pointer afterwards.
	virtual ULONG Release()                                                              = 0;

	virtual UINT Length() const                                                          = 0;
	virtual D3DFORMAT Format() const                                                     = 0;

protected:
	virtual ~IRHIIndexBuffer() = default; // destroy through Release()
};

#endif // N3BASE_RHI_RHIBUFFERS_H
