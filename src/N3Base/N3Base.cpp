// N3Base.cpp: implementation of the CN3Base class.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfxBase.h"
#include "N3Mesh.h"
#include "N3VMesh.h"
#include "N3PMesh.h"
#include "N3FXPMesh.h"
#include "N3FXShape.h"
#include "N3Chr.h"
#include "N3Base.h"
#include "RHI/RHIDevice.h"
#include <Platform/PlatformTime.h>
#ifndef _WIN32
#include <Platform/PlatformString.h>
#endif

LPDIRECT3DDEVICE9 CN3Base::s_lpD3DDev = nullptr;      // 참조 포인터.. 멋대로 해제하면 안된다..
IRHIDevice* CN3Base::s_pRHIDev         = nullptr;      // 렌더 백엔드 (RHI)
uint32_t CN3Base::s_dwTextureCaps     = 0;            // Texture 호환성..
float CN3Base::s_fFrmPerSec           = 30.0f;        // Frame Per Second
float CN3Base::s_fSecPerFrm           = 1.0f / 30.0f; // Second per Frame
HWND CN3Base::s_hWndBase              = nullptr;      // Init 할때 쓴 Window Handle
HWND CN3Base::s_hWndPresent           = nullptr;      // 최근에 Present 한 Window Handle

D3DPRESENT_PARAMETERS CN3Base::s_DevParam;            // Device 생성 Present Parameter
D3DCAPS9 CN3Base::s_DevCaps;                          // Device 호환성...
std::string CN3Base::s_szPath;

__CameraData CN3Base::s_CameraData;                   // Camera Data
__ResrcInfo CN3Base::s_ResrcInfo;                     // Rendering Information
__Options CN3Base::s_Options;                         // 각종 옵션등...
#ifdef _DEBUG
__RenderInfo CN3Base::s_RenderInfo;                   // Rendering Information
#endif

#ifdef _N3GAME                                        // 게임이 아닌 툴에서는 필요없다...
CN3SndMgr CN3Base::s_SndMgr;                          //사운드 메니저.
#endif
#ifdef _N3TOOL                                        // ui 에디터일때는 필요하다.
CN3SndMgr CN3Base::s_SndMgr;                          //사운드 메니저.
#endif

CN3Mng<CN3Texture> CN3Base::s_MngTex;                 // Texture Manager
CN3Mng<CN3Mesh> CN3Base::s_MngMesh;                   // Mesh Manager
CN3Mng<CN3VMesh> CN3Base::s_MngVMesh; // 단순히 폴리곤만 갖고 있는 메시 - 주로 충돌 체크에 쓴다..
CN3Mng<CN3PMesh> CN3Base::s_MngPMesh; // Progressive Mesh Manager
CN3Mng<CN3Joint> CN3Base::s_MngJoint; // Joint Manager
CN3Mng<CN3CPartSkins> CN3Base::s_MngSkins;    // Character Part Skins Manager
CN3Mng<CN3AnimControl> CN3Base::s_MngAniCtrl; // Animation Manager
CN3Mng<CN3FXPMesh>
	CN3Base::s_MngFXPMesh; // FX에서 쓰는 PMesh - 파일은 일반 PMesh를 쓰지만 속은 다르다.
CN3Mng<CN3FXShape>
	CN3Base::s_MngFXShape; // FX에서 쓰는 Shape - 파일은 일반 shape를 쓰지만 속은 다르다.

CN3AlphaPrimitiveManager CN3Base::
	s_AlphaMgr; // Alpha blend 할 폴리곤들을 관리.. 추가했다가.. 카메라 거리에 ?上?정렬하고 한꺼번에 그린다..

#ifdef _N3GAME
CLogWriter g_Log; // 로그 남기기...
#endif

CN3Base::CN3Base()
{
	m_dwType = OBJ_BASE; // "MESH", "CAMERA", "SCENE", "???" .... 등등등...
}

CN3Base::~CN3Base()
{
}

void CN3Base::Release()
{
	m_szName.clear();
}

void CN3Base::ReleaseResrc()
{
	s_MngTex.Release();
	s_MngMesh.Release();
	s_MngPMesh.Release();
	s_MngVMesh.Release();

	s_MngJoint.Release();
	s_MngSkins.Release();
	s_MngAniCtrl.Release();

	s_MngFXPMesh.Release();
	s_MngFXShape.Release();
}

#ifdef _N3TOOL
void CN3Base::SaveResrc()
{
	s_MngTex.SaveToFiles();
	s_MngMesh.SaveToFiles();
	s_MngPMesh.SaveToFiles();
	s_MngVMesh.SaveToFiles();

	s_MngJoint.SaveToFiles();
	s_MngSkins.SaveToFiles();
	s_MngAniCtrl.SaveToFiles();

	s_MngFXPMesh.SaveToFiles();
	s_MngFXShape.SaveToFiles();
}
#endif // end of _N3TOOL

//-----------------------------------------------------------------------------
// Name: TimerProcess()
// Desc: Performs timer operations (derived from the DXUtil_Timer sample).
//       Runs on std::chrono's monotonic clock (formerly
//       QueryPerformanceCounter with a timeGetTime() fallback), so it behaves
//       identically on every platform. Use the following commands:
//          TIMER_RESET           - to reset the timer
//          TIMER_START           - to start the timer
//          TIMER_STOP            - to stop (or pause) the timer
//          TIMER_ADVANCE         - to advance the timer by 0.1 seconds
//          TIMER_GETABSOLUTETIME - to get the absolute system time
//          TIMER_GETAPPTIME      - to get the current time
//          TIMER_GETELAPSEDTIME  - to get the time that elapsed between
//                                  TIMER_GETELAPSEDTIME calls
//-----------------------------------------------------------------------------
float CN3Base::TimerProcess(TIMER_COMMAND command)
{
	static double s_fStopTime        = 0.0;
	static double s_fLastElapsedTime = 0.0;
	static double s_fBaseTime        = 0.0;

	// Get either the current time or the stop time, depending
	// on whether we're stopped and what command was sent
	double fTime = 0.0;
	if (s_fStopTime != 0.0 && command != TIMER_START && command != TIMER_GETABSOLUTETIME)
		fTime = s_fStopTime;
	else
		fTime = PlatformTimeSeconds();

	switch (command)
	{
		// Return the elapsed time
		case TIMER_GETELAPSEDTIME:
		{
			double fElapsedTime = fTime - s_fLastElapsedTime;
			s_fLastElapsedTime  = fTime;
			return (float) fElapsedTime;
		}

		// Return the current time
		case TIMER_GETAPPTIME:
			return (float) (fTime - s_fBaseTime);

		// Reset the timer
		case TIMER_RESET:
			s_fBaseTime        = fTime;
			s_fLastElapsedTime = fTime;
			return 0.0f;

		// Start the timer
		case TIMER_START:
			s_fBaseTime        += fTime - s_fStopTime;
			s_fStopTime         = 0.0;
			s_fLastElapsedTime  = fTime;
			return 0.0f;

		// Stop the timer
		case TIMER_STOP:
			s_fStopTime        = fTime;
			s_fLastElapsedTime = fTime;
			return 0.0f;

		// Advance the timer by 1/10th second
		case TIMER_ADVANCE:
			s_fStopTime += 0.1;
			return 0.0f;

		case TIMER_GETABSOLUTETIME:
			return (float) fTime;

		default:
			return -1.0f; // Invalid command specified
	}
}

void CN3Base::PathSet(const std::string& szPath)
{
	s_szPath = szPath;
	if (s_szPath.size() <= 0)
		return;

	// NOTE: this puts the entire string into lowercase characters
#ifdef _WIN32
	CharLower(&(s_szPath[0])); // make sure to give lowercase
#else
	StrLowerAscii(s_szPath);   // make sure to give lowercase
#endif
	if (s_szPath.size() > 1)
	{
		// NOTE: this checks if the last character is the path separator; if not it will add it
#ifdef _WIN32
		if (s_szPath[s_szPath.size() - 1] != '\\')
			s_szPath += '\\';
#else
		if (s_szPath[s_szPath.size() - 1] != '/')
			s_szPath += '/';
#endif
	}
}

void CN3Base::RenderLines(const __Vector3* pvLines, int nCount, D3DCOLOR color)
{
	if (s_pRHIDev == nullptr)
		return;

	DWORD dwAlpha = 0, dwFog = 0, dwLight = 0;
	RHIDevice()->GetRenderState(D3DRS_FOGENABLE, &dwFog);
	RHIDevice()->GetRenderState(D3DRS_ALPHABLENDENABLE, &dwAlpha);
	RHIDevice()->GetRenderState(D3DRS_LIGHTING, &dwLight);

	if (dwFog)
		RHIDevice()->SetRenderState(D3DRS_FOGENABLE, FALSE);
	if (dwAlpha)
		RHIDevice()->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	if (dwLight)
		RHIDevice()->SetRenderState(D3DRS_LIGHTING, FALSE);

	static __Material smtl;
	static bool bInit = false;
	if (false == bInit)
	{
		smtl.Init();
		bInit = true;
	}

	RHIDevice()->SetTexture(0, nullptr);

	static __VertexColor svLines[512];

	RHIDevice()->SetFVF(FVF_CV);

	int nRepeat = nCount / 512;
	for (int i = 0; i < nRepeat; i++)
	{
		for (int j = 0; j < 512; j++)
			svLines[j].Set(
				pvLines[i * 512 + j].x, pvLines[i * 512 + j].y, pvLines[i * 512 + j].z, color);

		RHIDevice()->DrawPrimitiveUP(D3DPT_LINESTRIP, 511, svLines, sizeof(__VertexColor));
	}
	int nPC = nCount % 512;
	for (int j = 0; j < nPC + 1; j++)
		svLines[j].Set(pvLines[nRepeat * 512 + j].x, pvLines[nRepeat * 512 + j].y,
			pvLines[nRepeat * 512 + j].z, color);
	RHIDevice()->DrawPrimitiveUP(D3DPT_LINESTRIP, nPC, svLines, sizeof(__VertexColor)); // Y

	if (dwFog)
		RHIDevice()->SetRenderState(D3DRS_FOGENABLE, dwFog);
	if (dwAlpha)
		RHIDevice()->SetRenderState(D3DRS_ALPHABLENDENABLE, dwAlpha);
	if (dwLight)
		RHIDevice()->SetRenderState(D3DRS_LIGHTING, dwLight);
}

void CN3Base::RenderLines(const RECT& rc, D3DCOLOR color)
{
	if (s_pRHIDev == nullptr)
		return;

	static __VertexTransformedColor vLines[5];

	vLines[0].Set((float) rc.left, (float) rc.top, 0.9f, 1.0f, color);
	vLines[1].Set((float) rc.right, (float) rc.top, 0.9f, 1.0f, color);
	vLines[2].Set((float) rc.right, (float) rc.bottom, 0.9f, 1.0f, color);
	vLines[3].Set((float) rc.left, (float) rc.bottom, 0.9f, 1.0f, color);
	vLines[4] = vLines[0];

	DWORD dwZ = 0, dwFog = 0, dwAlpha = 0, dwCOP = 0, dwCA1 = 0, dwSrcBlend = 0, dwDestBlend = 0,
		  dwVertexShader = 0, dwAOP = 0, dwAA1 = 0;
	CN3Base::RHIDevice()->GetRenderState(D3DRS_ZENABLE, &dwZ);
	CN3Base::RHIDevice()->GetRenderState(D3DRS_FOGENABLE, &dwFog);
	CN3Base::RHIDevice()->GetRenderState(D3DRS_ALPHABLENDENABLE, &dwAlpha);
	CN3Base::RHIDevice()->GetRenderState(D3DRS_SRCBLEND, &dwSrcBlend);
	CN3Base::RHIDevice()->GetRenderState(D3DRS_DESTBLEND, &dwDestBlend);
	CN3Base::RHIDevice()->GetTextureStageState(0, D3DTSS_COLOROP, &dwCOP);
	CN3Base::RHIDevice()->GetTextureStageState(0, D3DTSS_COLORARG1, &dwCA1);
	CN3Base::RHIDevice()->GetTextureStageState(0, D3DTSS_ALPHAOP, &dwAOP);
	CN3Base::RHIDevice()->GetTextureStageState(0, D3DTSS_ALPHAARG1, &dwAA1);
	CN3Base::RHIDevice()->GetFVF(&dwVertexShader);

	CN3Base::RHIDevice()->SetRenderState(D3DRS_ZENABLE, FALSE);
	CN3Base::RHIDevice()->SetRenderState(D3DRS_FOGENABLE, FALSE);
	CN3Base::RHIDevice()->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	CN3Base::RHIDevice()->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	CN3Base::RHIDevice()->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	CN3Base::RHIDevice()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	CN3Base::RHIDevice()->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	CN3Base::RHIDevice()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	CN3Base::RHIDevice()->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

	CN3Base::RHIDevice()->SetFVF(FVF_TRANSFORMEDCOLOR);
	CN3Base::RHIDevice()->DrawPrimitiveUP(
		D3DPT_LINESTRIP, 4, vLines, sizeof(__VertexTransformedColor));

	CN3Base::RHIDevice()->SetRenderState(D3DRS_ZENABLE, dwZ);
	CN3Base::RHIDevice()->SetRenderState(D3DRS_FOGENABLE, dwFog);
	CN3Base::RHIDevice()->SetRenderState(D3DRS_ALPHABLENDENABLE, dwAlpha);
	CN3Base::RHIDevice()->SetRenderState(D3DRS_SRCBLEND, dwSrcBlend);
	CN3Base::RHIDevice()->SetRenderState(D3DRS_DESTBLEND, dwDestBlend);
	CN3Base::RHIDevice()->SetTextureStageState(0, D3DTSS_COLOROP, dwCOP);
	CN3Base::RHIDevice()->SetTextureStageState(0, D3DTSS_COLORARG1, dwCA1);
	CN3Base::RHIDevice()->SetTextureStageState(0, D3DTSS_ALPHAOP, dwAOP);
	CN3Base::RHIDevice()->SetTextureStageState(0, D3DTSS_ALPHAARG1, dwAA1);
	CN3Base::RHIDevice()->SetFVF(dwVertexShader);
}

float CN3Base::TimeGet()
{
	// Monotonic seconds since process start (formerly QueryPerformanceCounter
	// with a timeGetTime() fallback). Callers only ever consume differences.
	return (float) PlatformTimeSeconds();
}
