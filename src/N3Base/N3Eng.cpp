#include "StdAfxBase.h"
#include "N3Eng.h"
#ifdef _WIN32
#include "RHI/RHIDeviceD3D9.h"
#endif
#include "N3Light.h"
#include "LogWriter.h"

#include <bit>

CN3Eng::CN3Eng()
{
	m_lpD3D         = nullptr;
	s_lpD3DDev      = nullptr;
	m_nModeActive   = -1;
	m_nAdapterCount = 1;

	memset(&m_DeviceInfo, 0, sizeof(__D3DDEV_INFO));

	delete[] m_DeviceInfo.pModes;
	memset(&m_DeviceInfo, 0, sizeof(m_DeviceInfo));

#ifdef _N3GAME
	CLogWriter::Open("Log.txt");
#endif

#ifdef _WIN32
	m_lpD3D = Direct3DCreate9(D3D_SDK_VERSION);
	if (m_lpD3D == nullptr)
	{
#ifdef _N3GAME
		CLogWriter::Write("Direct3D9 is not installed or lower version");
#endif
		exit(-1);
	}

	// 프로그램이 실행된 경로..
	if (s_szPath.empty())
	{
		char szPath[_MAX_PATH] {}, szDrive[_MAX_DRIVE] {}, szDir[_MAX_DIR] {};
		::GetModuleFileName(nullptr, szPath, _MAX_PATH);
		_splitpath(szPath, szDrive, szDir, nullptr, nullptr);

		std::string newPath  = szDrive;
		newPath             += szDir;
		PathSet(newPath); // 경로 설정..
	}
#else
	// POSIX (docs/PORT_POSIX_PLAN.md, T6.8): the window/context and the RHI
	// backend (Null/GL) are created by the SDL entry point, not here. The base
	// run path is set by WarFareMainSDL before the engine is constructed.
#endif
}

CN3Eng::~CN3Eng()
{
	s_SndMgr.Release();

	CN3Base::ReleaseResrc();
	delete[] m_DeviceInfo.pModes;

#ifdef _WIN32
	// On Windows CN3Eng::Init created the RHI backend (RHIDeviceD3D9) over the
	// D3D device it owns, so it releases both here. On POSIX the RHI device is
	// created and owned by the SDL entry point, so CN3Eng must not touch it.
	delete s_pRHIDev;
	RHIDeviceSet(nullptr);

	if (s_lpD3DDev)
	{
		int nRefCount = s_lpD3DDev->Release();
		if (nRefCount == 0)
		{
			s_lpD3DDev = nullptr;
		}
		else
		{
#ifdef _N3GAME
			CLogWriter::Write("CNEng::~CN3Eng - Device reference count is bigger than 0");
#endif
		}
	}

	if (m_lpD3D != nullptr && m_lpD3D->Release() == 0)
		m_lpD3D = nullptr;
#endif

#ifdef _N3GAME
	CLogWriter::Close();
#endif
}

void CN3Eng::Release()
{
	m_nModeActive   = -1;
	m_nAdapterCount = 1;

	delete[] m_DeviceInfo.pModes;
	memset(&m_DeviceInfo, 0, sizeof(m_DeviceInfo));

#ifdef _WIN32
	// See ~CN3Eng: the RHI device is CN3Eng-owned on Windows, SDL-owned on POSIX.
	delete s_pRHIDev;
	RHIDeviceSet(nullptr);

	if (s_lpD3DDev)
	{
		int nRefCount = s_lpD3DDev->Release();

		if (nRefCount == 0)
		{
			s_lpD3DDev = nullptr;
		}
		else
		{
#ifdef _N3GAME
			CLogWriter::Write("CNEng::Release Device reference count is bigger than 0");
#endif
		}
	}
#endif
}

//-----------------------------------------------------------------------------
void CN3Eng::SetViewPort(RECT& rc)
{
	if (RHIDevice() == nullptr)
		return;

	D3DVIEWPORT9 vp;
	vp.X      = rc.left;
	vp.Y      = rc.top;
	vp.Width  = rc.right - rc.left;
	vp.Height = rc.bottom - rc.top;
	vp.MinZ   = 0.0f;
	vp.MaxZ   = 1.0f;

	RHIDevice()->SetViewport(&vp);
	memcpy(&s_CameraData.vp, &vp, sizeof(D3DVIEWPORT9));
}

void CN3Eng::SetDefaultEnvironment()
{
	__Matrix44 matWorld;
	matWorld.Identity();

	RHIDevice()->SetTransform(D3DTS_WORLD, matWorld.toD3D());
	RHIDevice()->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	RHIDevice()->SetRenderState(D3DRS_LIGHTING, TRUE);

	RHIDevice()->SetRenderState(D3DRS_DITHERENABLE, TRUE);
	RHIDevice()->SetRenderState(D3DRS_SPECULARENABLE, TRUE);

	RHIDevice()->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
	RHIDevice()->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);

	RHIDevice()->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	RHIDevice()->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);

	float fMipMapLODBias = -1.0f;

	for (int i = 0; i < 8; ++i)
	{
		RHIDevice()->SetTexture(i, nullptr);
		RHIDevice()->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		RHIDevice()->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		RHIDevice()->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
		RHIDevice()->SetSamplerState(
			i, D3DSAMP_MIPMAPLODBIAS, std::bit_cast<uint32_t>(fMipMapLODBias));
	}

	// 기본 라이트 정보 지정..
	for (int i = 0; i < 8; i++)
	{
		CN3Light::__Light Lgt;
		__ColorValue LgtColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		Lgt.InitPoint(i, { 0, 0, 0 }, LgtColor);
		RHIDevice()->SetLight(i, Lgt.toD3D());
	}

#ifdef _WIN32
	// D3D clip-status is a fixed-function optimization with no RHI/GL analogue.
	D3DCLIPSTATUS9 cs;
	cs.ClipUnion = cs.ClipIntersection = D3DCS_ALL;

	s_lpD3DDev->SetClipStatus(&cs);
#endif
}

/*!
Used to set the view matrix for DirectX
*/
void CN3Eng::LookAt(const __Vector3& vEye, const __Vector3& vAt, const __Vector3& vUp)
{
	__Matrix44 matView;
	matView.LookAtLH(vEye, vAt, vUp);
	RHIDevice()->SetTransform(D3DTS_VIEW, matView.toD3D());
}

//-----------------------------------------------------------------------------
#ifdef _WIN32
bool CN3Eng::Reset(bool bWindowed, uint32_t dwWidth, uint32_t dwHeight, uint32_t dwBPP)
{
	if (s_lpD3DDev == nullptr)
		return false;
	if (dwWidth <= 0 || dwHeight <= 0)
		return false;

	if (dwWidth == s_DevParam.BackBufferWidth && dwHeight == s_DevParam.BackBufferHeight)
	{
		if (0 == dwBPP)
			return false;
		if (16 == dwBPP && D3DFMT_R5G6B5 == s_DevParam.BackBufferFormat)
			return false;
		if (24 == dwBPP && D3DFMT_R8G8B8 == s_DevParam.BackBufferFormat)
			return false;
		if (32 == dwBPP && D3DFMT_X8R8G8B8 == s_DevParam.BackBufferFormat)
			return false;
	}

	D3DPRESENT_PARAMETERS DevParamBackUp;
	memcpy(&DevParamBackUp, &s_DevParam, sizeof(D3DPRESENT_PARAMETERS));

	D3DFORMAT BBFormat = D3DFMT_UNKNOWN;

	if (bWindowed)
	{
		D3DDISPLAYMODE dm;
		m_lpD3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &dm);

		BBFormat = dm.Format;
	}
	else
	{
		if (16 == dwBPP)
			BBFormat = D3DFMT_R5G6B5;
		else if (24 == dwBPP)
			BBFormat = D3DFMT_R8G8B8;
		else if (32 == dwBPP)
			BBFormat = D3DFMT_X8R8G8B8;
	}

	s_DevParam.Windowed         = bWindowed;
	s_DevParam.BackBufferWidth  = dwWidth;
	s_DevParam.BackBufferHeight = dwHeight;
	s_DevParam.BackBufferFormat = BBFormat;

	int nMC                     = m_DeviceInfo.nModeCount;
	for (int i = 0; i < nMC; i++)
	{
		if (m_DeviceInfo.pModes[i].Format == s_DevParam.BackBufferFormat)
		{
			FindDepthStencilFormat(0, m_DeviceInfo.DevType, m_DeviceInfo.pModes[i].Format,
				&s_DevParam.AutoDepthStencilFormat);

			m_nModeActive = i;

			break;
		}
	}

	if (D3D_OK != s_lpD3DDev->Reset(&s_DevParam))
	{
#ifdef _N3GAME
		CLogWriter::Write("CNEng::Reset - Insufficient video memory");
#endif
		memcpy(&s_DevParam, &DevParamBackUp, sizeof(D3DPRESENT_PARAMETERS));

		if (D3D_OK != s_lpD3DDev->Reset(&s_DevParam))
		{
#ifdef _N3GAME
			CLogWriter::Write("CNEng::Reset - Insufficient video memory");
#endif
		}

		return false;
	}

	RECT rcView = { 0, 0, (int) dwWidth, (int) dwHeight };

	SetViewPort(rcView);
	SetDefaultEnvironment();

	return true;
}
#else
bool CN3Eng::Reset(bool /*bWindowed*/, uint32_t /*dwWidth*/, uint32_t /*dwHeight*/, uint32_t /*dwBPP*/)
{
	// POSIX (docs/PORT_POSIX_PLAN.md, T6.8): swapchain/backbuffer resizing is
	// owned by the SDL window + RHI backend, not by a D3D device reset.
	return false;
}
#endif

//-----------------------------------------------------------------------------
/*!
Set the projection matrix for DirectX
*/
void CN3Eng::SetProjection(float fNear, float fFar, float fLens, float fAspect)
{
	__Matrix44 matProjection;
	matProjection.PerspectiveFovLH(fLens, fAspect, fNear, fFar);
	RHIDevice()->SetTransform(D3DTS_PROJECTION, matProjection.toD3D());
}

#ifdef _WIN32
bool CN3Eng::Init(
	BOOL bWindowed, HWND hWnd, uint32_t dwWidth, uint32_t dwHeight, uint32_t dwBPP, BOOL bUseHW)
{
	s_ResrcInfo        = {}; // Rendering Information 초기화..

	s_hWndBase         = hWnd;

	// FIX (srmeier): I really have no idea what the second arguement here should be
	int nModesX8R8G8B8 = m_lpD3D->GetAdapterModeCount(0, D3DFMT_X8R8G8B8);
	int nModesR8G8B8   = m_lpD3D->GetAdapterModeCount(0, D3DFMT_R8G8B8);
	int nModesR5G6B5   = m_lpD3D->GetAdapterModeCount(0, D3DFMT_R5G6B5);

	// 디스플레이 모드 카운트
	int nAMC           = nModesX8R8G8B8 + nModesR8G8B8 + nModesR5G6B5;
	if (nAMC <= 0)
	{
		//MessageBox(hWnd, "Can't create D3D - Invalid display mode property.", "initialization", MB_OK);
//		{ for(int iii = 0; iii < 2; iii++) Beep(2000, 200); Sleep(300); } // 여러번 삑~
#ifdef _N3GAME
		CLogWriter::Write("Can't create D3D - Invalid display mode property.");
#endif
		this->Release();
		return false;
	}

	m_DeviceInfo.nAdapter   = 0;
	m_DeviceInfo.DevType    = D3DDEVTYPE_HAL;
	m_DeviceInfo.nDevice    = 0;
	m_DeviceInfo.nModeCount = nAMC;

	delete[] m_DeviceInfo.pModes;
	m_DeviceInfo.pModes = new D3DDISPLAYMODE[nAMC];
	if (m_DeviceInfo.pModes == nullptr)
	{
		Release();
		return false;
	}

	// 디스플레이 모드 가져오기..
	int nModeOffset = 0;
	for (int i = 0; i < nModesX8R8G8B8; i++)
		m_lpD3D->EnumAdapterModes(0, D3DFMT_X8R8G8B8, i, &m_DeviceInfo.pModes[nModeOffset++]);

	for (int i = 0; i < nModesR8G8B8; i++)
		m_lpD3D->EnumAdapterModes(0, D3DFMT_R8G8B8, i, &m_DeviceInfo.pModes[nModeOffset++]);

	for (int i = 0; i < nModesR5G6B5; i++)
		m_lpD3D->EnumAdapterModes(0, D3DFMT_R5G6B5, i, &m_DeviceInfo.pModes[nModeOffset++]);

	D3DDEVTYPE DevType = D3DDEVTYPE_REF;
	if (TRUE == bUseHW)
		DevType = D3DDEVTYPE_HAL;

	memset(&s_DevParam, 0, sizeof(s_DevParam));
	s_DevParam.Windowed                   = bWindowed;
	s_DevParam.EnableAutoDepthStencil     = TRUE;
	s_DevParam.SwapEffect                 = D3DSWAPEFFECT_DISCARD;
	s_DevParam.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;

	s_DevParam.PresentationInterval       = s_Options.bVSyncEnabled ? D3DPRESENT_INTERVAL_ONE
																	: D3DPRESENT_INTERVAL_IMMEDIATE;

	D3DFORMAT BBFormat                    = D3DFMT_UNKNOWN;
	if (bWindowed) // 윈도우 모드일 경우
	{
		D3DDISPLAYMODE dm;
		m_lpD3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &dm);
		s_DevParam.BackBufferCount = 1;

		if (dwWidth <= 0)
			dwWidth = dm.Width;

		if (dwHeight <= 0)
			dwHeight = dm.Height;

		BBFormat                 = dm.Format;
		s_DevParam.hDeviceWindow = s_hWndBase;
	}
	else
	{
		s_DevParam.BackBufferCount        = 1;
		s_DevParam.AutoDepthStencilFormat = D3DFMT_D16; // 자동 생성이면 무시된다.

		if (16 == dwBPP)
			BBFormat = D3DFMT_R5G6B5;
		else if (24 == dwBPP)
			BBFormat = D3DFMT_R8G8B8;
		else if (32 == dwBPP)
			BBFormat = D3DFMT_X8R8G8B8;

		s_DevParam.hDeviceWindow = s_hWndBase;
	}

	s_DevParam.BackBufferWidth  = dwWidth;
	s_DevParam.BackBufferHeight = dwHeight;
	s_DevParam.BackBufferFormat = BBFormat;
	s_DevParam.MultiSampleType =
		D3DMULTISAMPLE_NONE; // Swap Effect 가 Discard 형태가 아니면 반드시 이런 식이어야 한다.
	s_DevParam.Flags = 0;
	//#ifdef _N3TOOL
	s_DevParam.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
	//#endif // end of _N3TOOL

	int nMC          = m_DeviceInfo.nModeCount;
	for (int i = 0; i < nMC; i++)
	{
		//		if(	m_DeviceInfo.pModes[i].Width == dwWidth &&
		//			m_DeviceInfo.pModes[i].Height == dwHeight &&
		if (m_DeviceInfo.pModes[i].Format == BBFormat) // 모드가 일치하면
		{
			this->FindDepthStencilFormat(0, m_DeviceInfo.DevType, m_DeviceInfo.pModes[i].Format,
				&s_DevParam.AutoDepthStencilFormat);   // 깊이와 스텐실 버퍼를 찾는다.
			m_nModeActive = i;
			break;
		}
	}

	HRESULT rval = m_lpD3D->CreateDevice(
		0, DevType, s_hWndBase, D3DCREATE_HARDWARE_VERTEXPROCESSING, &s_DevParam, &s_lpD3DDev);
	if (rval != D3D_OK)
	{
		rval = m_lpD3D->CreateDevice(
			0, DevType, s_hWndBase, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &s_DevParam, &s_lpD3DDev);
		if (rval != D3D_OK)
		{
			MessageBox(s_hWndBase,
				"Can't create D3D Device - please, check DirectX or display card driver",
				"initialization", MB_OK);
#ifdef _N3GAME
			CLogWriter::Write(
				"Can't create D3D Device - please, check DirectX or display card driver");
			CLogWriter::Write("CreateDevice() error code: {:X}", rval);
#endif

			Release();
			return false;
		}
#ifdef _N3GAME
		CLogWriter::Write("CNEng::Init - Not supported HardWare TnL");
#endif
	}

	// Install the RHI backend over the freshly created device
	// (docs/PORT_POSIX_PLAN.md, phase 5): render code migrates from
	// s_lpD3DDev-> to RHIDevice()->.
	delete s_pRHIDev;
	RHIDeviceSet(new RHIDeviceD3D9(s_lpD3DDev));

	// Device 지원 항목은??
	// DXT 지원 여부..
	s_dwTextureCaps      = 0;
	s_DevCaps.DeviceType = DevType;

	s_lpD3DDev->GetDeviceCaps(&s_DevCaps);
	if (s_DevCaps.MaxTextureWidth < 256
		|| s_DevCaps.MaxTextureHeight < 256) // 텍스처 지원 크기가 256 이하면.. 아예 포기..
	{
		MessageBox(s_hWndBase, "Can't support this graphic card : Texture size is too small",
			"Initialization error", MB_OK);
#ifdef _N3GAME
		CLogWriter::Write("Can't support this graphic card : Texture size is too small");
#endif
		//		{ for(int iii = 0; iii < 4; iii++) Beep(2000, 200); Sleep(300); } // 여러번 삑~

		this->Release();
		return false;
	}

	if (D3D_OK
		== m_lpD3D->CheckDeviceFormat(
			D3DADAPTER_DEFAULT, DevType, BBFormat, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT1))
		s_dwTextureCaps |= TEX_CAPS_DXT1;
	if (D3D_OK
		== m_lpD3D->CheckDeviceFormat(
			D3DADAPTER_DEFAULT, DevType, BBFormat, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT2))
		s_dwTextureCaps |= TEX_CAPS_DXT2;
	if (D3D_OK
		== m_lpD3D->CheckDeviceFormat(
			D3DADAPTER_DEFAULT, DevType, BBFormat, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT3))
		s_dwTextureCaps |= TEX_CAPS_DXT3;
	if (D3D_OK
		== m_lpD3D->CheckDeviceFormat(
			D3DADAPTER_DEFAULT, DevType, BBFormat, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT4))
		s_dwTextureCaps |= TEX_CAPS_DXT4;
	if (D3D_OK
		== m_lpD3D->CheckDeviceFormat(
			D3DADAPTER_DEFAULT, DevType, BBFormat, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT5))
		s_dwTextureCaps |= TEX_CAPS_DXT5;
	if (s_DevCaps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY)
		s_dwTextureCaps |= TEX_CAPS_SQUAREONLY;
	if (s_DevCaps.TextureCaps & D3DPTEXTURECAPS_MIPMAP)
		s_dwTextureCaps |= TEX_CAPS_MIPMAP;
	if (s_DevCaps.TextureCaps & D3DPTEXTURECAPS_POW2)
		s_dwTextureCaps |= TEX_CAPS_POW2;

	// 기본 뷰와 프로젝션 설정.
	this->LookAt(__Vector3(5, 5, -10), __Vector3(0, 0, 0), __Vector3(0, 1, 0));
	this->SetProjection(0.1f, 256.0f, DegreesToRadians(45.0f), (float) dwHeight / dwWidth);

	RECT rcView = { 0, 0, (int) dwWidth, (int) dwHeight };
	this->SetViewPort(rcView);
	this->SetDefaultEnvironment(); // 기본 상태로 설정..

	return true;
}

/*
LRESULT WINAPI CN3Eng::MsgProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
    switch( msg )
    {
        case WM_DESTROY:
//          PostQuitMessage( 0 );
            return 0;

        case WM_PAINT:
//          Render();
//          ValidateRect( hWnd, nullptr );
            return 0;
    }

    return DefWindowProc( hWnd, msg, wParam, lParam );
}
*/

BOOL CN3Eng::FindDepthStencilFormat(
	UINT iAdapter, D3DDEVTYPE DeviceType, D3DFORMAT TargetFormat, D3DFORMAT* pDepthStencilFormat)
{
	int nDSC              = 6;
	D3DFORMAT DepthFmts[] = { D3DFMT_D32, D3DFMT_D24S8, D3DFMT_D24X4S4, D3DFMT_D24X8, D3DFMT_D16,
		D3DFMT_D15S1 };

	HRESULT rval          = 0;
	for (int i = 0; i < nDSC; i++)
	{
		rval = m_lpD3D->CheckDeviceFormat(iAdapter, DeviceType, TargetFormat, D3DUSAGE_DEPTHSTENCIL,
			D3DRTYPE_SURFACE, DepthFmts[i]);
		if (D3D_OK == rval)
		{
			rval = m_lpD3D->CheckDepthStencilMatch(
				iAdapter, DeviceType, TargetFormat, TargetFormat, DepthFmts[i]);
			if (D3D_OK == rval)
			{
				*pDepthStencilFormat = DepthFmts[i];
				return TRUE;
			}
		}
	}

	return FALSE;
}
#else  // !_WIN32
bool CN3Eng::Init(BOOL /*bWindowed*/, HWND /*hWnd*/, uint32_t dwWidth, uint32_t dwHeight,
	uint32_t /*dwBPP*/, BOOL /*bUseHW*/)
{
	// POSIX (docs/PORT_POSIX_PLAN.md, T6.8): the SDL entry point owns window +
	// GL/Null context creation and installs the RHI backend before the engine
	// runs, so there is no D3D adapter enumeration to perform here.
	if (RHIDevice() == nullptr)
		return false;

	// The Windows path queries the D3D adapter for device caps and texture
	// format support.  On POSIX the RHI backend (GL or Null) handles these
	// transparently, so we just advertise sensible defaults.
	s_DevCaps.MaxTextureWidth  = 4096;
	s_DevCaps.MaxTextureHeight = 4096;
	s_DevCaps.TextureCaps      = 0;

	// GL (via S3TC) and the Null backend both accept DXT-compressed surfaces.
	s_dwTextureCaps = TEX_CAPS_DXT1 | TEX_CAPS_DXT2 | TEX_CAPS_DXT3
					| TEX_CAPS_DXT4 | TEX_CAPS_DXT5 | TEX_CAPS_MIPMAP;

	// Viewport + default render states — identical to the tail of the Win32 Init.
	this->LookAt(__Vector3(5, 5, -10), __Vector3(0, 0, 0), __Vector3(0, 1, 0));
	this->SetProjection(0.1f, 256.0f, DegreesToRadians(45.0f),
		static_cast<float>(dwHeight) / static_cast<float>(dwWidth));

	RECT rcView = {0, 0, static_cast<int>(dwWidth), static_cast<int>(dwHeight)};
	this->SetViewPort(rcView);
	this->SetDefaultEnvironment();

	return true;
}

BOOL CN3Eng::FindDepthStencilFormat(UINT /*iAdapter*/, D3DDEVTYPE /*DeviceType*/,
	D3DFORMAT /*TargetFormat*/, D3DFORMAT* /*pDepthStencilFormat*/)
{
	return FALSE;
}
#endif // _WIN32

#ifdef _WIN32
#ifndef _N3TOOL
void CN3Eng::Present(HWND hWnd, RECT* pRC)
{
	RECT rc;
	if (s_DevParam.Windowed) // 윈도우 모드면...
	{
		GetClientRect(s_hWndBase, &rc);
		pRC = &rc;
	}

	HRESULT rval = s_lpD3DDev->Present(pRC, pRC, hWnd, nullptr);
	if (D3D_OK == rval)
	{
		s_hWndPresent = hWnd; // Present window handle 을 저장해 놓는다.
	}
	else if (D3DERR_DEVICELOST == rval || D3DERR_DEVICENOTRESET == rval)
	{
		rval = s_lpD3DDev->Reset(&s_DevParam);
		if (D3D_OK != rval)
		{
#ifdef _N3GAME
			// NOTE: Officially it's ErrCode(%d) but this is horrendously useless
			CLogWriter::Write("Device Present ErrCode({:X})", rval);
#endif

			WaitForDeviceRestoration();
		}

		s_lpD3DDev->Present(pRC, pRC, hWnd, nullptr);
	}

	////////////////////////////////////////////////////////////////////////////////
	// 프레임 율 측정...
	s_fSecPerFrm = CN3Base::TimerProcess(TIMER_GETELAPSEDTIME);

	// 너무 안나오면 기본 값인 30 프레임으로 맞춘다..
	if (s_fSecPerFrm <= 0.001f || s_fSecPerFrm >= 1.0f)
		s_fSecPerFrm = 0.033333f;

	s_fFrmPerSec = 1.0f / s_fSecPerFrm; // 초당 프레임 수 측정..

										//	fTimePrev = fTime;
	// 프레임 율 측정...
	////////////////////////////////////////////////////////////////////////////////
}
#else // _N3TOOL
void CN3Eng::Present(HWND hWnd, RECT* pRC)
{
	RECT rc;
	if (s_DevParam.Windowed)
	{
		GetClientRect(hWnd, &rc);
		pRC = &rc;
	}

	HRESULT rval = s_lpD3DDev->Present(pRC, pRC, hWnd, nullptr);
	if (D3D_OK == rval)
	{
		s_hWndPresent = hWnd; // Present window handle 을 저장해 놓는다.
	}
	else if (D3DERR_DEVICELOST == rval || D3DERR_DEVICENOTRESET == rval)
	{
		rval = s_lpD3DDev->Reset(&s_DevParam);
		if (D3D_OK != rval)
			WaitForDeviceRestoration();

		rval = s_lpD3DDev->Present(pRC, pRC, hWnd, nullptr);
	}

	s_fSecPerFrm = CN3Base::TimerProcess(TIMER_GETELAPSEDTIME);

	// 너무 안나오면 기본 값인 30 프레임으로 맞춘다..
	if (s_fSecPerFrm <= 0.001f || s_fSecPerFrm >= 1.0f)
		s_fSecPerFrm = 0.033333f;

	s_fFrmPerSec = 1.0f / s_fSecPerFrm; // 초당 프레임 수 측정..
}
#endif

void CN3Eng::WaitForDeviceRestoration()
{
	while (true)
	{
		HRESULT rval = s_lpD3DDev->TestCooperativeLevel();
		if (rval == D3DERR_DEVICENOTRESET || rval == D3D_OK)
		{
			rval = s_lpD3DDev->Reset(&s_DevParam);
			if (rval == D3D_OK)
			{
#ifdef _N3GAME
				CLogWriter::Write("Device reset succeeded");
#endif
				SetDefaultEnvironment();
				break;
			}

#ifdef _N3GAME
			// NOTE: Officially it's ErrCode(%d) but this is horrendously useless
			CLogWriter::Write("Device reset failed - ErrCode(:X)", rval);
#endif
		}

		MSG msg {};
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		Sleep(2000);
	}
}

void CN3Eng::Clear(D3DCOLOR crFill, RECT* pRC)
{
	RECT rc;
	if (pRC == nullptr && s_DevParam.Windowed) // 윈도우 모드면...
	{
		GetClientRect(s_hWndBase, &rc);
		pRC = &rc;
	}

	if (pRC != nullptr)
	{
		_D3DRECT rc3D = { pRC->left, pRC->top, pRC->right, pRC->bottom };
		s_lpD3DDev->Clear(1, &rc3D, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET, crFill, 1.0f, 0);
	}
	else
	{
		s_lpD3DDev->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET, crFill, 1.0f, 0);
	}

#ifdef _DEBUG
	s_RenderInfo = {};
#endif
}

void CN3Eng::ClearAuto(RECT* pRC)
{
	DWORD dwFillColor = D3DCOLOR_ARGB(255, 192, 192, 192); // 기본색
	DWORD dwUseFog    = FALSE;
	s_lpD3DDev->GetRenderState(
		D3DRS_FOGENABLE, &dwUseFog); // 안개를 쓰면 바탕색을 안개색을 깔아준다..
	if (dwUseFog != 0)
		s_lpD3DDev->GetRenderState(D3DRS_FOGCOLOR, &dwFillColor);
	else
	{
		CN3Light::__Light Lgt;

		BOOL bEnable = FALSE;
		s_lpD3DDev->GetLightEnable(0, &bEnable);
		if (bEnable)
		{
			s_lpD3DDev->GetLight(0, Lgt.toD3D());
			dwFillColor = D3DCOLOR_ARGB((uint8_t) (Lgt.Diffuse.a * 255.0f),
				(uint8_t) (Lgt.Diffuse.r * 255.0f), (uint8_t) (Lgt.Diffuse.g * 255.0f),
				(uint8_t) (Lgt.Diffuse.b * 255.0f));
		}
	}

	CN3Eng::Clear(dwFillColor, pRC);
}

void CN3Eng::ClearZBuffer(const RECT* pRC)
{
	RECT rc;
	if (pRC == nullptr && s_DevParam.Windowed) // 윈도우 모드면...
	{
		GetClientRect(s_hWndBase, &rc);
		pRC = &rc;
	}

	if (pRC != nullptr)
	{
		D3DRECT rc3D = { pRC->left, pRC->top, pRC->right, pRC->bottom };
		s_lpD3DDev->Clear(1, &rc3D, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
	}
	else
	{
		s_lpD3DDev->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
	}
}
#else  // !_WIN32
// POSIX present/clear go straight through the RHI backend (Null/GL). The SDL
// entry point owns the actual buffer swap; here Present only drives the frame
// timer so the engine's per-frame delta stays correct. Rect-limited clears
// (the D3D windowed-mode GetClientRect path) collapse to a full-target clear,
// which is what the RHI Clear exposes.
void CN3Eng::Present(HWND /*hWnd*/, RECT* /*pRC*/)
{
	if (RHIDevice() != nullptr)
		RHIDevice()->Present();

	s_fSecPerFrm = CN3Base::TimerProcess(TIMER_GETELAPSEDTIME);

	if (s_fSecPerFrm <= 0.001f || s_fSecPerFrm >= 1.0f)
		s_fSecPerFrm = 0.033333f;

	s_fFrmPerSec = 1.0f / s_fSecPerFrm;
}

void CN3Eng::WaitForDeviceRestoration()
{
	// No D3D device-lost state on POSIX backends.
}

void CN3Eng::Clear(D3DCOLOR crFill, RECT* /*pRC*/)
{
	if (RHIDevice() != nullptr)
		RHIDevice()->Clear(D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET, crFill, 1.0f, 0);

#ifdef _DEBUG
	s_RenderInfo = {};
#endif
}

void CN3Eng::ClearAuto(RECT* pRC)
{
	DWORD dwFillColor = D3DCOLOR_ARGB(255, 192, 192, 192); // 기본색
	DWORD dwUseFog    = FALSE;
	if (RHIDevice() != nullptr)
	{
		RHIDevice()->GetRenderState(D3DRS_FOGENABLE, &dwUseFog);
		if (dwUseFog != 0)
		{
			RHIDevice()->GetRenderState(D3DRS_FOGCOLOR, &dwFillColor);
		}
		else
		{
			CN3Light::__Light Lgt;

			BOOL bEnable = FALSE;
			RHIDevice()->GetLightEnable(0, &bEnable);
			if (bEnable)
			{
				RHIDevice()->GetLight(0, Lgt.toD3D());
				dwFillColor = D3DCOLOR_ARGB((uint8_t) (Lgt.Diffuse.a * 255.0f),
					(uint8_t) (Lgt.Diffuse.r * 255.0f), (uint8_t) (Lgt.Diffuse.g * 255.0f),
					(uint8_t) (Lgt.Diffuse.b * 255.0f));
			}
		}
	}

	CN3Eng::Clear(dwFillColor, pRC);
}

void CN3Eng::ClearZBuffer(const RECT* /*pRC*/)
{
	if (RHIDevice() != nullptr)
		RHIDevice()->Clear(D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
}
#endif // _WIN32
