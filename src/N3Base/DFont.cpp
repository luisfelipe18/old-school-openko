// DFont.cpp: implementation of the CDFont class.
//
//////////////////////////////////////////////////////////////////////
#include "StdAfxBase.h"
#include "DFont.h"

#ifdef _WIN32
const int MAX_NUM_VERTICES   = 50 * 6;
const float Z_DEFAULT        = 0.9f;
const float RHW_DEFAULT      = 1.0f;
#endif
//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
HDC CDFont::s_hDC            = nullptr;
int CDFont::s_iInstanceCount = 0;
HFONT CDFont::s_hFontOld     = nullptr;

#ifdef _WIN32

CDFont::CDFont(const std::string& szFontName, uint32_t dwHeight, uint32_t dwFlags)
{
	if (0 == s_iInstanceCount)
	{
		s_hDC       = CreateCompatibleDC(nullptr);

		// 임시 폰트를 만들고 s_hFontOld를 얻는다.
		HFONT hFont = CreateFont(0, 0, 0, 0, 0, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH, "굴림");
		if (hFont != nullptr)
		{
			s_hFontOld = (HFONT) (SelectObject(s_hDC, hFont));
			SelectObject(s_hDC, s_hFontOld);
			DeleteObject(hFont);
		}
	}
	s_iInstanceCount++;

	__ASSERT(!szFontName.empty(), "??");

	m_szFontName      = szFontName;
	m_dwFontHeight    = dwHeight;
	m_dwFontFlags     = dwFlags;

	m_dwTexHeight     = 0;
	m_dwTexWidth      = 0;
	m_fTextScale      = 1.0f;

	m_pd3dDevice      = nullptr;
	m_pTexture        = nullptr;
	m_pVB             = nullptr;

	m_iPrimitiveCount = 0;
	m_PrevLeftTop.x = m_PrevLeftTop.y = 0;

	m_hFont                           = nullptr;
	m_dwFontColor                     = 0xffffffff;
	m_Size.cx                         = 0;
	m_Size.cy                         = 0;
}

CDFont::~CDFont()
{
	InvalidateDeviceObjects();
	DeleteDeviceObjects();

	s_iInstanceCount--;
	if (s_iInstanceCount <= 0)
	{
		if (s_hFontOld)
			SelectObject(s_hDC, s_hFontOld);
		DeleteDC(s_hDC);
		s_hDC = nullptr;
	}
}

HRESULT CDFont::SetFont(const std::string& szFontName, uint32_t dwHeight, uint32_t dwFlags)
{
	__ASSERT(!szFontName.empty(), "");
	if (nullptr == s_hDC)
	{
		__ASSERT(0, "NULL DC Handle");
		return E_FAIL;
	}

	m_szFontName   = szFontName;
	m_dwFontHeight = dwHeight;
	m_dwFontFlags  = dwFlags;

	if (m_hFont)
	{
		if (s_hFontOld)
			SelectObject(s_hDC, s_hFontOld);
		DeleteObject(m_hFont);
		m_hFont = nullptr;
	}

	// Create a font.  By specifying ANTIALIASED_QUALITY, we might get an
	// antialiased font, but this is not guaranteed.
	INT nHeight = -MulDiv(
		m_dwFontHeight, (INT) (GetDeviceCaps(s_hDC, LOGPIXELSY) * m_fTextScale), 72);
	uint32_t dwBold   = (m_dwFontFlags & D3DFONT_BOLD) ? FW_BOLD : FW_NORMAL;
	uint32_t dwItalic = (m_dwFontFlags & D3DFONT_ITALIC) ? TRUE : FALSE;
	m_hFont = CreateFont(nHeight, 0, 0, 0, dwBold, dwItalic, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH,
		m_szFontName.c_str());
	if (nullptr == m_hFont)
	{
		__ASSERT(0, "NULL Font Handle");
		return E_FAIL;
	}
	return S_OK;
}

HRESULT CDFont::InitDeviceObjects(LPDIRECT3DDEVICE9 pd3dDevice)
{
	// Keep a local copy of the device
	m_pd3dDevice = pd3dDevice;
	m_fTextScale = 1.0f; // Draw fonts into texture without scaling

	return S_OK;
}

HRESULT CDFont::RestoreDeviceObjects()
{
	HRESULT hr        = S_OK;

	m_iPrimitiveCount = 0;

	//	__ASSERT(nullptr == s_hDC && nullptr == m_hFont, "??");
	//	m_hDC = CreateCompatibleDC(nullptr);
	__ASSERT(nullptr == m_hFont, "??");

	if (nullptr == s_hDC)
	{
		__ASSERT(0, "Can't Create DC");
		return E_FAIL;
	}
	SetMapMode(s_hDC, MM_TEXT);

	// Create a font.  By specifying ANTIALIASED_QUALITY, we might get an
	// antialiased font, but this is not guaranteed.
	INT nHeight = -MulDiv(
		m_dwFontHeight, (INT) (GetDeviceCaps(s_hDC, LOGPIXELSY) * m_fTextScale), 72);
	uint32_t dwBold   = (m_dwFontFlags & D3DFONT_BOLD) ? FW_BOLD : FW_NORMAL;
	uint32_t dwItalic = (m_dwFontFlags & D3DFONT_ITALIC) ? TRUE : FALSE;
	m_hFont = CreateFont(nHeight, 0, 0, 0, dwBold, dwItalic, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH,
		m_szFontName.c_str());
	if (nullptr == m_hFont)
		return E_FAIL;

	// Create vertex buffer for the letters
	__ASSERT(m_pVB == nullptr, "??");
	int iVBSize    = MAX_NUM_VERTICES * sizeof(__VertexTransformed);
	uint32_t dwFVF = FVF_TRANSFORMED;
	hr = m_pd3dDevice->CreateVertexBuffer(iVBSize, 0, dwFVF, D3DPOOL_MANAGED, &m_pVB, nullptr);
	if (FAILED(hr))
		return hr;

	return S_OK;
}

HRESULT CDFont::InvalidateDeviceObjects()
{
	if (m_pVB)
	{
		m_pVB->Release();
		m_pVB = nullptr;
	}

	if (m_hFont)
	{
		if (s_hDC && s_hFontOld)
			SelectObject(s_hDC, s_hFontOld);
		DeleteObject(m_hFont);
		m_hFont = nullptr;
	}
	return S_OK;
}

HRESULT CDFont::DeleteDeviceObjects()
{
	if (m_pTexture)
	{
		m_pTexture->Release();
		m_pTexture = nullptr;
	}
	m_pd3dDevice = nullptr;

	return S_OK;
}

HRESULT CDFont::SetText(const std::string& szText, uint32_t dwFlags)
{
	if (nullptr == s_hDC || nullptr == m_hFont)
		return E_FAIL;

	if (szText.empty())
	{
		m_iPrimitiveCount = 0;
		if (m_pTexture)
		{
			m_pTexture->Release();
			m_pTexture = nullptr;
		}
		return S_OK;
	}

	int iStrLen    = static_cast<int>(szText.size());

	HRESULT hr     = S_OK;
	// \n을 빼고 한줄로 만들어서 글자 길이 계산하기
	int iCount     = 0;
	int iTempCount = 0;
	SIZE size;

	std::string szTemp(iStrLen, '?');
	while (iCount < iStrLen)
	{
		if ('\n' == szText[iCount]) // \n
		{
			++iCount;
		}
		else if (0x80 & szText[iCount]) // 2BYTE 문자
		{
			if ((iCount + 2) > iStrLen) // 이상한 문자열이다..
			{
				//				__ASSERT(0, "이상한 문자열이다.!!!");
				break;
			}
			else
			{
				memcpy(&(szTemp[iTempCount]), &(szText[iCount]), 2);
				iTempCount += 2;
				iCount     += 2;
			}
		}
		else // 1BYTE 문자
		{
			memcpy(&(szTemp[iTempCount]), &(szText[iCount]), 1);
			++iTempCount;
			++iCount;
		}
		__ASSERT(iCount <= iStrLen, "??"); // 이상한 문자가 들어왔을 경우
	}

										   //	szTemp[iTempCount] = 0x00;

	// 텍스쳐 사이즈 결정하기
	SelectObject(s_hDC, m_hFont);
	GetTextExtentPoint32(s_hDC, szTemp.c_str(), static_cast<int>(szTemp.size()), &size);
	szTemp = "";

	if (size.cx <= 0 || size.cy <= 0)
	{
		__ASSERT(0, "Invalid Text Size - ?????");
		return E_FAIL;
	}
	int iExtent = size.cx * size.cy;

	SIZE size2; // 한글 반글자의 크기..
	GetTextExtentPoint32(s_hDC, "진", lstrlen("진"), &size2);
	size2.cx         = ((size2.cx / 2) + (size2.cx % 2));

	int iTexSizes[7] = { 32, 64, 128, 256, 512, 1024, 2048 };
	for (int i = 0; i < 7; ++i)
	{
		if (iExtent <= (iTexSizes[i] - size2.cx - size2.cy - 1) * iTexSizes[i])
		{
			m_dwTexWidth = m_dwTexHeight = iTexSizes[i];
			break;
		}
	}

	// Establish the font and texture size
	m_fTextScale = 1.0f; // Draw fonts into texture without scaling

	// If requested texture is too big, use a smaller texture and smaller font,
	// and scale up when rendering.
	D3DCAPS9 d3dCaps;
	m_pd3dDevice->GetDeviceCaps(&d3dCaps);

	if (m_dwTexWidth > d3dCaps.MaxTextureWidth)
	{
		m_fTextScale = (float) d3dCaps.MaxTextureWidth / (float) m_dwTexWidth;
		m_dwTexWidth = m_dwTexHeight = d3dCaps.MaxTextureWidth;
	}

	// 기존 텍스쳐 크기가 새로 만들 텍스쳐 크기와 다를 경우 다시 만든다.
	if (m_pTexture)
	{
		D3DSURFACE_DESC sd;
		ZeroMemory(&sd, sizeof(sd));
		m_pTexture->GetLevelDesc(0, &sd);
		if (sd.Width != m_dwTexWidth)
		{
			m_pTexture->Release();
			m_pTexture = nullptr;
		}
	}

	// Create a new texture for the font
	if (nullptr == m_pTexture)
	{
		int iMipMapCount = 1;
		if (dwFlags & D3DFONT_FILTERED)
			iMipMapCount = 0; // 필터링 텍스트는 밉맵을 만든다..

		hr = m_pd3dDevice->CreateTexture(m_dwTexWidth, m_dwTexHeight, iMipMapCount, 0,
			D3DFMT_A4R4G4B4, D3DPOOL_MANAGED, &m_pTexture, nullptr);
		if (FAILED(hr))
			return hr;
	}

	// Prepare to create a bitmap
	uint32_t* pBitmapBits = nullptr;
	BITMAPINFO bmi {};
	bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth       = (int) m_dwTexWidth;
	bmi.bmiHeader.biHeight      = -(int) m_dwTexHeight;
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biCompression = BI_RGB;
	bmi.bmiHeader.biBitCount    = 32;

	// Create a DC and a bitmap for the font
	HBITMAP hbmBitmap           = CreateDIBSection(
        s_hDC, &bmi, DIB_RGB_COLORS, (VOID**) &pBitmapBits, nullptr, 0);

	if (nullptr == hbmBitmap)
	{
		__ASSERT(0, "CreateDIBSection 실패");
		if (m_pTexture)
		{
			m_pTexture->Release();
			m_pTexture = nullptr;
		}
		return E_FAIL;
	}

	HGDIOBJ hObjPrev = ::SelectObject(s_hDC, hbmBitmap);

	// Set text properties
	SetTextColor(s_hDC, RGB(255, 255, 255));
	SetBkColor(s_hDC, RGB(0, 0, 0));
	SetTextAlign(s_hDC, TA_TOP);

	// Loop through all printable character and output them to the bitmap..
	// Meanwhile, keep track of the corresponding tex coords for each character.
	Make2DVertex(size.cy, szText);

	// Lock the surface and write the alpha values for the set pixels
	D3DLOCKED_RECT d3dlr;
	m_pTexture->LockRect(0, &d3dlr, nullptr, 0);
	uint16_t* pDst16 = (uint16_t*) d3dlr.pBits;

	for (uint32_t y = 0; y < m_dwTexHeight; y++)
	{
		for (uint32_t x = 0; x < m_dwTexWidth; x++)
		{
			// 4-bit measure of pixel intensity
			uint8_t bAlpha = (uint8_t) ((pBitmapBits[m_dwTexWidth * y + x] & 0xff) >> 4);
			if (bAlpha > 0)
				*pDst16++ = (bAlpha << 12) | 0x0fff;
			else
				*pDst16++ = 0;
		}
	}

	// Done updating texture, so clean up used objects
	m_pTexture->UnlockRect(0);

	::SelectObject(s_hDC, hObjPrev); // 반드시 전의걸 선택해야..
	DeleteObject(hbmBitmap);         // 제대로 지워진다..

	////////////////////////////////////////////////////////////
	// 필터링 텍스처는... MipMap 만든다..
	if (dwFlags & D3DFONT_FILTERED)
	{
		int iMMC = m_pTexture->GetLevelCount();
		for (int i = 1; i < iMMC; i++)
		{
			LPDIRECT3DSURFACE9 lpSurfSrc  = nullptr;
			LPDIRECT3DSURFACE9 lpSurfDest = nullptr;
			m_pTexture->GetSurfaceLevel(i - 1, &lpSurfSrc);
			m_pTexture->GetSurfaceLevel(i, &lpSurfDest);

			if (lpSurfSrc && lpSurfDest)
			{
				::D3DXLoadSurfaceFromSurface(lpSurfDest, nullptr, nullptr, lpSurfSrc, nullptr,
					nullptr, D3DX_FILTER_TRIANGLE, 0); // 서피스 복사
			}

			if (lpSurfSrc)
				lpSurfSrc->Release();
			if (lpSurfDest)
				lpSurfDest->Release();
		}
	}
	// 필터링 텍스처는... MipMap 만든다..
	////////////////////////////////////////////////////////////

	return S_OK;
}

void CDFont::Make2DVertex(const int iFontHeight, const std::string& szText)
{
	if (m_pVB == nullptr || s_hDC == nullptr || m_hFont == nullptr)
	{
		__ASSERT(0, "NULL Vertex Buffer or DC or Font Handle ");
		return;
	}

	if (szText.empty())
		return;

	// D3D9's -0.5 half-pixel offset; GL/SDL_GPU map integer coords to pixel
	// centres already (see IRHIDevice::NeedsHalfPixelOffset). Kept consistent
	// with N3UIImage so text stays aligned to its backgrounds on every backend.
	const float fHP = (RHIDevice() != nullptr && RHIDevice()->NeedsHalfPixelOffset()) ? 0.5f : 0.0f;

	int iStrLen                    = static_cast<int>(szText.size());

	// lock vertex buffer
	__VertexTransformed* pVertices = nullptr;
	uint32_t dwNumTriangles        = 0;
	if (FAILED(m_pVB->Lock(0, 0, (void**) &pVertices, 0)))
		return;

	uint32_t sx        = 0; // start x y
	uint32_t x         = 0;
	uint32_t y         = 0;
	float vtx_sx       = 0;
	float vtx_sy       = 0; //	vertex start x y
	int iCount         = 0;

	char szTempChar[3] = "";
	uint32_t dwColor   = 0xffffffff; // 폰트의 색
	m_dwFontColor      = 0xffffffff;
	SIZE size;

	float fMaxX = 0.0f, fMaxY = 0.0f; // 글씨가 찍히는 범위의 최대 최소값을 조사하기 위해서.

	while (iCount < iStrLen)
	{
		if ('\n' == szText[iCount]) // \n
		{
			++iCount;

			// vertex 만들기
			if (sx != x)
			{
				FLOAT tx1 = ((FLOAT) (sx)) / m_dwTexWidth;
				FLOAT ty1 = ((FLOAT) (y)) / m_dwTexHeight;
				FLOAT tx2 = ((FLOAT) (x)) / m_dwTexWidth;
				FLOAT ty2 = ((FLOAT) (y + iFontHeight)) / m_dwTexHeight;

				FLOAT w   = (tx2 - tx1) * m_dwTexWidth / m_fTextScale;
				FLOAT h   = (ty2 - ty1) * m_dwTexHeight / m_fTextScale;

				__ASSERT(dwNumTriangles + 2 < MAX_NUM_VERTICES, "??"); // Vertex buffer가 모자란다.
				if (dwNumTriangles + 2 >= MAX_NUM_VERTICES)
					break;

				FLOAT fLeft   = vtx_sx + 0 - fHP;
				FLOAT fRight  = vtx_sx + w - fHP;
				FLOAT fTop    = vtx_sy + 0 - fHP;
				FLOAT fBottom = vtx_sy + h - fHP;
				pVertices->Set(fLeft, fBottom, Z_DEFAULT, RHW_DEFAULT, dwColor, tx1, ty2);
				++pVertices;
				pVertices->Set(fLeft, fTop, Z_DEFAULT, RHW_DEFAULT, dwColor, tx1, ty1);
				++pVertices;
				pVertices->Set(fRight, fBottom, Z_DEFAULT, RHW_DEFAULT, dwColor, tx2, ty2);
				++pVertices;
				pVertices->Set(fRight, fTop, Z_DEFAULT, RHW_DEFAULT, dwColor, tx2, ty1);
				++pVertices;
				pVertices->Set(fRight, fBottom, Z_DEFAULT, RHW_DEFAULT, dwColor, tx2, ty2);
				++pVertices;
				pVertices->Set(fLeft, fTop, Z_DEFAULT, RHW_DEFAULT, dwColor, tx1, ty1);
				++pVertices;

				dwNumTriangles += 2;

				if (fMaxX < fRight)
					fMaxX = fRight;
				if (fMaxY < fBottom)
					fMaxY = fBottom;
			}
			// 화면의 다음 줄로 넘기기
			sx     = x;
			vtx_sx = 0;
			vtx_sy = vtx_sy + ((float) (iFontHeight)) / m_fTextScale;
			continue;
		}
		else if (0x80 & szText[iCount]) // 2BYTE 문자
		{
			memcpy(szTempChar, &(szText[iCount]), 2);
			iCount        += 2;
			szTempChar[2]  = 0x00;
		}
		else // 1BYTE 문자
		{
			memcpy(szTempChar, &(szText[iCount]), 1);
			iCount        += 1;
			szTempChar[1]  = 0x00;
		}

		SelectObject(s_hDC, m_hFont);
		GetTextExtentPoint32(s_hDC, szTempChar, lstrlen(szTempChar), &size);
		if ((x + size.cx) > m_dwTexWidth)
		{ // vertex 만들고 다음 줄로 넘기기..
			// vertex 만들기
			if (sx != x)
			{
				FLOAT tx1 = ((FLOAT) (sx)) / m_dwTexWidth;
				FLOAT ty1 = ((FLOAT) (y)) / m_dwTexHeight;
				FLOAT tx2 = ((FLOAT) (x)) / m_dwTexWidth;
				FLOAT ty2 = ((FLOAT) (y + iFontHeight)) / m_dwTexHeight;

				FLOAT w   = (tx2 - tx1) * m_dwTexWidth / m_fTextScale;
				FLOAT h   = (ty2 - ty1) * m_dwTexHeight / m_fTextScale;

				__ASSERT(dwNumTriangles + 2 < MAX_NUM_VERTICES, "??"); // Vertex buffer가 모자란다.
				if (dwNumTriangles + 2 >= MAX_NUM_VERTICES)
					break;

				FLOAT fLeft   = vtx_sx + 0 - fHP;
				FLOAT fRight  = vtx_sx + w - fHP;
				FLOAT fTop    = vtx_sy + 0 - fHP;
				FLOAT fBottom = vtx_sy + h - fHP;
				pVertices->Set(fLeft, fBottom, Z_DEFAULT, RHW_DEFAULT, dwColor, tx1, ty2);
				++pVertices;
				pVertices->Set(fLeft, fTop, Z_DEFAULT, RHW_DEFAULT, dwColor, tx1, ty1);
				++pVertices;
				pVertices->Set(fRight, fBottom, Z_DEFAULT, RHW_DEFAULT, dwColor, tx2, ty2);
				++pVertices;
				pVertices->Set(fRight, fTop, Z_DEFAULT, RHW_DEFAULT, dwColor, tx2, ty1);
				++pVertices;
				pVertices->Set(fRight, fBottom, Z_DEFAULT, RHW_DEFAULT, dwColor, tx2, ty2);
				++pVertices;
				pVertices->Set(fLeft, fTop, Z_DEFAULT, RHW_DEFAULT, dwColor, tx1, ty1);
				++pVertices;
				dwNumTriangles += 2;

				if (fMaxX < fRight)
					fMaxX = fRight;
				if (fMaxY < fBottom)
					fMaxY = fBottom;

				// 텍스쳐의 다음 줄로 넘기기
				x = sx  = 0;
				y      += iFontHeight;
				vtx_sx  = vtx_sx + w;
			}
			else
			{
				x = sx  = 0;
				y      += iFontHeight;
			}
		}

		// dc에 찍기
		SelectObject(s_hDC, m_hFont);
		ExtTextOut(s_hDC, x, y, ETO_OPAQUE, nullptr, szTempChar, lstrlen(szTempChar), nullptr);
		x += size.cx;
	}

	// 마지막 남은 vertex 만들기
	if (sx != x)
	{
		FLOAT tx1 = ((FLOAT) (sx)) / m_dwTexWidth;
		FLOAT ty1 = ((FLOAT) (y)) / m_dwTexHeight;
		FLOAT tx2 = ((FLOAT) (x)) / m_dwTexWidth;
		FLOAT ty2 = ((FLOAT) (y + iFontHeight)) / m_dwTexHeight;

		FLOAT w   = (tx2 - tx1) * m_dwTexWidth / m_fTextScale;
		FLOAT h   = (ty2 - ty1) * m_dwTexHeight / m_fTextScale;

		__ASSERT(dwNumTriangles + 2 < MAX_NUM_VERTICES, "??"); // Vertex buffer가 모자란다.

		FLOAT fLeft   = vtx_sx + 0 - fHP;
		FLOAT fRight  = vtx_sx + w - fHP;
		FLOAT fTop    = vtx_sy + 0 - fHP;
		FLOAT fBottom = vtx_sy + h - fHP;
		pVertices->Set(fLeft, fBottom, Z_DEFAULT, RHW_DEFAULT, dwColor, tx1, ty2);
		++pVertices;
		pVertices->Set(fLeft, fTop, Z_DEFAULT, RHW_DEFAULT, dwColor, tx1, ty1);
		++pVertices;
		pVertices->Set(fRight, fBottom, Z_DEFAULT, RHW_DEFAULT, dwColor, tx2, ty2);
		++pVertices;
		pVertices->Set(fRight, fTop, Z_DEFAULT, RHW_DEFAULT, dwColor, tx2, ty1);
		++pVertices;
		pVertices->Set(fRight, fBottom, Z_DEFAULT, RHW_DEFAULT, dwColor, tx2, ty2);
		++pVertices;
		pVertices->Set(fLeft, fTop, Z_DEFAULT, RHW_DEFAULT, dwColor, tx1, ty1);
		++pVertices;
		dwNumTriangles += 2;

		if (fMaxX < fRight)
			fMaxX = fRight;

		if (fMaxY < fBottom)
			fMaxY = fBottom;
	}

	// Unlock and render the vertex buffer
	m_pVB->Unlock();

	m_iPrimitiveCount = dwNumTriangles;
	m_PrevLeftTop     = {};
	m_Size.cx         = (long) fMaxX;
	m_Size.cy         = (long) fMaxY;
}

HRESULT CDFont::DrawText(FLOAT sx, FLOAT sy, uint32_t dwColor, uint32_t dwFlags)
{
	if (m_pVB == nullptr || s_hDC == nullptr || m_hFont == nullptr)
	{
		//__ASSERT(0, "NULL Vertex Buffer or DC or Font Handle ");
		return E_FAIL;
	}

	if (m_iPrimitiveCount <= 0)
		return S_OK;
	if (m_pd3dDevice == nullptr)
		return E_FAIL;

	// 위치 색 조정
	__Vector2 vDiff = __Vector2(sx, sy) - m_PrevLeftTop;
	if (fabs(vDiff.x) > 0.5f || fabs(vDiff.y) > 0.5f || dwColor != m_dwFontColor)
	{
		// lock vertex buffer
		__VertexTransformed* pVertices = nullptr;
		HRESULT hr                     = m_pVB->Lock(0, 0, (void**) &pVertices, 0);
		if (FAILED(hr))
			return E_FAIL;

		int iVC = m_iPrimitiveCount * 3;
		if (fabs(vDiff.x) > 0.5f)
		{
			m_PrevLeftTop.x = sx;

			for (int i = 0; i < iVC; i++)
				pVertices[i].x += vDiff.x;
		}

		if (fabs(vDiff.y) > 0.5f)
		{
			m_PrevLeftTop.y = sy;

			for (int i = 0; i < iVC; i++)
				pVertices[i].y += vDiff.y;
		}

		if (dwColor != m_dwFontColor)
		{
			m_dwFontColor   = dwColor;
			m_PrevLeftTop.y = sy;

			for (int i = 0; i < iVC; i++)
				pVertices[i].color = m_dwFontColor;
		}

		// Unlock
		m_pVB->Unlock();
	}

	// back up render state
	DWORD dwAlphaBlend = 0, dwSrcBlend = 0, dwDestBlend = 0, dwZEnable = 0, dwFog = 0;
	DWORD dwColorOp = 0, dwColorArg1 = 0, dwColorArg2 = 0, dwAlphaOp = 0, dwAlphaArg1 = 0,
		  dwAlphaArg2 = 0, dwMinFilter = 0, dwMagFilter = 0;

	m_pd3dDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &dwAlphaBlend);
	m_pd3dDevice->GetRenderState(D3DRS_SRCBLEND, &dwSrcBlend);
	m_pd3dDevice->GetRenderState(D3DRS_DESTBLEND, &dwDestBlend);
	m_pd3dDevice->GetRenderState(D3DRS_ZENABLE, &dwZEnable);
	m_pd3dDevice->GetRenderState(D3DRS_FOGENABLE, &dwFog);

	m_pd3dDevice->GetTextureStageState(0, D3DTSS_COLOROP, &dwColorOp);
	m_pd3dDevice->GetTextureStageState(0, D3DTSS_COLORARG1, &dwColorArg1);
	m_pd3dDevice->GetTextureStageState(0, D3DTSS_COLORARG2, &dwColorArg2);
	m_pd3dDevice->GetTextureStageState(0, D3DTSS_ALPHAOP, &dwAlphaOp);
	m_pd3dDevice->GetTextureStageState(0, D3DTSS_ALPHAARG1, &dwAlphaArg1);
	m_pd3dDevice->GetTextureStageState(0, D3DTSS_ALPHAARG2, &dwAlphaArg2);
	m_pd3dDevice->GetSamplerState(0, D3DSAMP_MINFILTER, &dwMinFilter);
	m_pd3dDevice->GetSamplerState(0, D3DSAMP_MAGFILTER, &dwMagFilter);

	// Set up renderstate
	if (TRUE != dwAlphaBlend)
		m_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	if (D3DBLEND_SRCALPHA != dwSrcBlend)
		m_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	if (D3DBLEND_INVSRCALPHA != dwDestBlend)
		m_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

	if (D3DZB_FALSE != dwZEnable)
		m_pd3dDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);

	if (FALSE != dwFog)
		m_pd3dDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
	if (D3DTOP_MODULATE != dwColorOp)
		m_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	if (D3DTA_TEXTURE != dwColorArg1)
		m_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	if (D3DTA_DIFFUSE != dwColorArg2)
		m_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	if (D3DTOP_MODULATE != dwAlphaOp)
		m_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	if (D3DTA_TEXTURE != dwAlphaArg1)
		m_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	if (D3DTA_DIFFUSE != dwAlphaArg2)
		m_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	if (dwFlags & D3DFONT_FILTERED)
	{
		// Set filter states
		if (D3DTEXF_LINEAR != dwMinFilter)
			m_pd3dDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		if (D3DTEXF_LINEAR != dwMagFilter)
			m_pd3dDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	}
	else
	{
		if (D3DTEXF_POINT != dwMinFilter)
			m_pd3dDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		if (D3DTEXF_POINT != dwMagFilter)
			m_pd3dDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	}

	// render
	m_pd3dDevice->SetFVF(FVF_TRANSFORMED);
	m_pd3dDevice->SetStreamSource(0, m_pVB, 0, sizeof(__VertexTransformed));
	m_pd3dDevice->SetTexture(0, m_pTexture);
	m_pd3dDevice->DrawPrimitive(D3DPT_TRIANGLELIST, 0, m_iPrimitiveCount);

	// Restore the modified renderstates
	if (TRUE != dwAlphaBlend)
		m_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, dwAlphaBlend);
	if (D3DBLEND_SRCALPHA != dwSrcBlend)
		m_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, dwSrcBlend);
	if (D3DBLEND_INVSRCALPHA != dwDestBlend)
		m_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, dwDestBlend);

	if (D3DZB_FALSE != dwZEnable)
		m_pd3dDevice->SetRenderState(D3DRS_ZENABLE, dwZEnable);

	if (FALSE != dwFog)
		m_pd3dDevice->SetRenderState(D3DRS_FOGENABLE, dwFog);
	if (D3DTOP_MODULATE != dwColorOp)
		m_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, dwColorOp);
	if (D3DTA_TEXTURE != dwColorArg1)
		m_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, dwColorArg1);
	if (D3DTA_DIFFUSE != dwColorArg2)
		m_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, dwColorArg2);
	if (D3DTOP_MODULATE != dwAlphaOp)
		m_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, dwAlphaOp);
	if (D3DTA_TEXTURE != dwAlphaArg1)
		m_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, dwAlphaArg1);
	if (D3DTA_DIFFUSE != dwAlphaArg2)
		m_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, dwAlphaArg2);
	if (dwFlags & D3DFONT_FILTERED)
	{
		if (D3DTEXF_LINEAR != dwMinFilter)
			m_pd3dDevice->SetSamplerState(0, D3DSAMP_MINFILTER, dwMinFilter);
		if (D3DTEXF_LINEAR != dwMagFilter)
			m_pd3dDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, dwMagFilter);
	}
	else
	{
		if (D3DSAMP_MINFILTER != dwMinFilter)
			m_pd3dDevice->SetSamplerState(0, D3DSAMP_MINFILTER, dwMinFilter);
		if (D3DSAMP_MAGFILTER != dwMagFilter)
			m_pd3dDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, dwMagFilter);
	}

	return S_OK;
}

BOOL CDFont::GetTextExtent(const std::string& szString, int iStrLen, SIZE* pSize)
{
	if (nullptr == s_hDC)
		return FALSE;

	SelectObject(s_hDC, m_hFont);
	return ::GetTextExtentPoint32(s_hDC, szString.c_str(), iStrLen, pSize);
}

HRESULT CDFont::SetFontColor(uint32_t dwColor)
{
	if (m_iPrimitiveCount <= 0 || m_pVB == nullptr)
		return E_FAIL;

	if (dwColor == m_dwFontColor)
		return S_OK;

	__VertexTransformed* pVertices = nullptr;

	// lock vertex buffer
	HRESULT hr                     = m_pVB->Lock(0, 0, (void**) &pVertices, 0);
	if (FAILED(hr))
		return hr;

	m_dwFontColor = dwColor;

	int iVC       = m_iPrimitiveCount * 3;
	for (int i = 0; i < iVC; ++i)
		pVertices[i].color = m_dwFontColor;
	m_pVB->Unlock();

	return S_OK;
}

#else // _WIN32 --------------------------------------------------------------

// POSIX text backend (docs/PORT_POSIX_PLAN.md, T7.1): FreeType replaces the
// GDI rasterizer with the same design - SetText renders the whole string into
// a per-instance A4R4G4B4 texture (through the RHI) and builds one XYZRHW quad
// per texture-row run; DrawText translates/tints those quads and draws them
// with alpha blending. Game strings arrive in CP949 and are converted to
// Unicode codepoints at this boundary (PlatformEncoding).

#include "RHI/RHIDevice.h"
#include "RHI/RHITextures.h"

#include <Platform/PlatformEncoding.h>

#include <spdlog/spdlog.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

namespace
{
constexpr float Z_DEFAULT   = 0.9f;
constexpr float RHW_DEFAULT = 1.0f;

// One shared FreeType library + face for every CDFont instance (the game only
// ever asks for "굴림"/Gulim variants); refcounted through s_iInstanceCount
// like the shared GDI DC on Windows. The face is re-sized per operation.
FT_Library s_ftLibrary   = nullptr;
FT_Face s_ftFace         = nullptr;
bool s_bFontPathResolved = false;

// The classic GDI default the Windows path effectively renders at.
constexpr uint32_t FONT_DPI = 96;

std::string ResolveFontFile()
{
	namespace fs = std::filesystem;
	std::error_code ec;

	// A Fonts/ directory next to the game data always wins, so users can drop
	// in the exact face they want (e.g. a real Gulim or Noto Sans KR).
	const fs::path fontsDir = fs::path(CN3Base::PathGet()) / "Fonts";
	std::vector<fs::path> candidates;
	if (fs::is_directory(fontsDir, ec))
	{
		for (const auto& entry : fs::directory_iterator(fontsDir, ec))
		{
			std::string ext = entry.path().extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (ext == ".ttf" || ext == ".otf" || ext == ".ttc")
				candidates.push_back(entry.path());
		}
		std::sort(candidates.begin(), candidates.end());
		if (!candidates.empty())
			return candidates.front().string();
	}

	// System fonts with Hangul coverage first (the game is Korean at heart),
	// then common Latin fallbacks so text still shows without CJK fonts.
	static const char* SYSTEM_FONTS[] = {
		// macOS
		"/System/Library/Fonts/AppleSDGothicNeo.ttc",
		"/System/Library/Fonts/Supplemental/AppleGothic.ttf",
		"/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
		"/System/Library/Fonts/Helvetica.ttc",
		// Linux
		"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/opentype/noto/NotoSansCJK.ttc",
		"/usr/share/fonts/truetype/noto/NotoSansKR-Regular.ttf",
		"/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
		"/usr/share/fonts/TTF/DejaVuSans.ttf",
	};

	for (const char* szPath : SYSTEM_FONTS)
	{
		if (fs::exists(szPath, ec))
			return szPath;
	}

	return {};
}

// Lazily creates the shared face; returns nullptr when no usable font exists
// (text simply stays invisible, like the T6.8 stub did).
FT_Face SharedFace()
{
	if (s_ftFace != nullptr)
		return s_ftFace;
	if (s_ftLibrary == nullptr || s_bFontPathResolved) // resolved and failed
		return nullptr;

	s_bFontPathResolved      = true;

	const std::string szPath = ResolveFontFile();
	if (szPath.empty())
	{
		spdlog::warn("DFont: no usable font found; text will not render. "
					 "Drop a .ttf into '{}Fonts/' to fix this.",
			CN3Base::PathGet());
		return nullptr;
	}

	if (FT_New_Face(s_ftLibrary, szPath.c_str(), 0, &s_ftFace) != 0)
	{
		spdlog::warn("DFont: FreeType could not open '{}'; text will not render.", szPath);
		s_ftFace = nullptr;
		return nullptr;
	}

	spdlog::info("DFont: using font '{}'", szPath);
	return s_ftFace;
}

// Points -> pixels at the fixed 96 DPI the Windows path uses (MulDiv(h,96,72)).
FT_Face SizedFace(uint32_t dwFontHeight)
{
	FT_Face pFace = SharedFace();
	if (pFace == nullptr || dwFontHeight == 0)
		return nullptr;

	if (FT_Set_Char_Size(pFace, 0, static_cast<FT_F26Dot6>(dwFontHeight) * 64, FONT_DPI, FONT_DPI)
		!= 0)
		return nullptr;

	return pFace;
}

int LineHeight(FT_Face pFace)
{
	return static_cast<int>((pFace->size->metrics.ascender - pFace->size->metrics.descender + 63)
							>> 6);
}

// Decode game text to Unicode codepoints, preserving '\n'. On POSIX text
// reaches DFont from two sources - UTF-8 (the edit buffer and network-facing
// UI strings after NetToLocal, docs/PORT_POSIX_PLAN.md T7.3) and CP949
// (asset strings loaded straight off disk). We validate as UTF-8 first and
// only fall back to CP949->UTF-8, so a CP949 byte that happens to look like
// a UTF-8 lead byte doesn't get misdecoded when the payload is really UTF-8.
std::u32string DecodeGameText(std::string_view szText)
{
	if (szText.empty())
		return {};

	bool bAscii = true;
	for (const char c : szText)
	{
		if (static_cast<unsigned char>(c) & 0x80)
		{
			bAscii = false;
			break;
		}
	}

	std::u32string codepoints;
	if (bAscii)
	{
		codepoints.assign(szText.begin(), szText.end());
		return codepoints;
	}

	// Structural UTF-8 validation: every multi-byte lead is followed by the
	// right number of 10xxxxxx continuation bytes, and no overlong 2-byte
	// sequence. Cheap enough to run every SetText - a hit means we can skip
	// the iconv round-trip entirely.
	auto IsValidUtf8 = [](std::string_view sv) -> bool {
		for (size_t i = 0; i < sv.size();)
		{
			const auto b0 = static_cast<unsigned char>(sv[i]);
			size_t len    = 1;
			if (b0 < 0x80)
			{
				++i;
				continue;
			}
			else if ((b0 & 0xE0) == 0xC0)
				len = 2;
			else if ((b0 & 0xF0) == 0xE0)
				len = 3;
			else if ((b0 & 0xF8) == 0xF0)
				len = 4;
			else
				return false;

			if (i + len > sv.size())
				return false;
			for (size_t k = 1; k < len; ++k)
			{
				if ((static_cast<unsigned char>(sv[i + k]) & 0xC0) != 0x80)
					return false;
			}
			if (len == 2 && b0 < 0xC2)
				return false; // overlong
			i += len;
		}
		return true;
	};

	std::string_view utf8View;
	std::string utf8Storage;
	if (IsValidUtf8(szText))
	{
		utf8View = szText; // already UTF-8 (UI/chat/edit buffer)
	}
	else
	{
		// Likely CP949 (asset). Convert; Latin-1 byte-cast is the final
		// fallback so a mis-encoded legacy asset still shows something.
		utf8Storage = Cp949ToUtf8(szText);
		if (utf8Storage.empty())
		{
			codepoints.reserve(szText.size());
			for (const char c : szText)
				codepoints.push_back(static_cast<unsigned char>(c));
			return codepoints;
		}
		utf8View = utf8Storage;
	}

	// Minimal UTF-8 decode; the input is guaranteed well-formed by the
	// checks above (or by Cp949ToUtf8, which drops invalid sequences).
	codepoints.reserve(utf8View.size());
	for (size_t i = 0; i < utf8View.size();)
	{
		const auto b0 = static_cast<unsigned char>(utf8View[i]);
		char32_t cp   = 0;
		size_t len    = 1;
		if (b0 < 0x80)
		{
			cp = b0;
		}
		else if ((b0 & 0xE0) == 0xC0 && i + 1 < utf8View.size())
		{
			cp  = static_cast<char32_t>(b0 & 0x1F);
			len = 2;
		}
		else if ((b0 & 0xF0) == 0xE0 && i + 2 < utf8View.size())
		{
			cp  = static_cast<char32_t>(b0 & 0x0F);
			len = 3;
		}
		else if ((b0 & 0xF8) == 0xF0 && i + 3 < utf8View.size())
		{
			cp  = static_cast<char32_t>(b0 & 0x07);
			len = 4;
		}
		else
		{
			++i;
			continue;
		}

		for (size_t k = 1; k < len; ++k)
			cp = (cp << 6) | (static_cast<unsigned char>(utf8View[i + k]) & 0x3F);

		codepoints.push_back(cp);
		i += len;
	}

	return codepoints;
}

// Unhinted metrics to match the unhinted rasterization below; the fractional
// advance is rounded (not truncated) so long runs don't squeeze together.
int GlyphAdvance(FT_Face pFace, char32_t cp)
{
	if (FT_Load_Char(pFace, cp, FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING) != 0)
		return 0;
	return static_cast<int>((pFace->glyph->advance.x + 32) >> 6);
}

// Extent of the text as one line with '\n' stripped - the exact measure GDI's
// GetTextExtentPoint32 gave SetText for picking the texture size.
SIZE MeasureStripped(FT_Face pFace, const std::u32string& codepoints)
{
	SIZE size = { 0, LineHeight(pFace) };
	for (const char32_t cp : codepoints)
	{
		if (cp != U'\n')
			size.cx += GlyphAdvance(pFace, cp);
	}
	return size;
}
} // namespace

bool CDFont::HasUsableFont()
{
	// The shared library normally lives between the first CDFont ctor and the
	// last dtor; bring it up temporarily if asked before any instance exists.
	const bool bTempLib = (s_ftLibrary == nullptr);
	if (bTempLib && FT_Init_FreeType(&s_ftLibrary) != 0)
	{
		s_ftLibrary = nullptr;
		return false;
	}

	const bool bUsable = (SharedFace() != nullptr);

	if (bTempLib && s_iInstanceCount == 0)
	{
		if (s_ftFace != nullptr)
		{
			FT_Done_Face(s_ftFace);
			s_ftFace = nullptr;
		}
		FT_Done_FreeType(s_ftLibrary);
		s_ftLibrary         = nullptr;
		s_bFontPathResolved = false;
	}

	return bUsable;
}

CDFont::CDFont(const std::string& szFontName, uint32_t dwHeight, uint32_t dwFlags)
	: m_szFontName(szFontName), m_dwFontHeight(dwHeight), m_dwFontFlags(dwFlags),
	  m_pd3dDevice(nullptr), m_pTexture(nullptr), m_dwTexWidth(0), m_dwTexHeight(0),
	  m_fTextScale(1.0f), m_hFont(nullptr), m_iPrimitiveCount(0), m_dwFontColor(0xffffffff)
{
	if (0 == s_iInstanceCount)
	{
		if (FT_Init_FreeType(&s_ftLibrary) != 0)
		{
			spdlog::warn("DFont: FT_Init_FreeType failed; text will not render");
			s_ftLibrary = nullptr;
		}
	}
	++s_iInstanceCount;

	m_PrevLeftTop.Set(0.0f, 0.0f);
	m_Size.cx = m_Size.cy = 0;
}

CDFont::~CDFont()
{
	InvalidateDeviceObjects();
	DeleteDeviceObjects();

	--s_iInstanceCount;
	if (s_iInstanceCount <= 0)
	{
		if (s_ftFace != nullptr)
		{
			FT_Done_Face(s_ftFace);
			s_ftFace = nullptr;
		}
		if (s_ftLibrary != nullptr)
		{
			FT_Done_FreeType(s_ftLibrary);
			s_ftLibrary = nullptr;
		}
		s_bFontPathResolved = false;
	}
}

HRESULT CDFont::SetFont(const std::string& szFontName, uint32_t dwHeight, uint32_t dwFlags)
{
	// One shared face backs every requested family; bold/italic synthesis is
	// still TODO (the login UI does not use it).
	m_szFontName   = szFontName;
	m_dwFontHeight = dwHeight;
	m_dwFontFlags  = dwFlags;
	return S_OK;
}

HRESULT CDFont::InitDeviceObjects(LPDIRECT3DDEVICE9 pd3dDevice)
{
	m_pd3dDevice = pd3dDevice; // legacy handle; rendering goes through RHIDevice()
	m_fTextScale = 1.0f;
	return S_OK;
}

HRESULT CDFont::RestoreDeviceObjects()
{
	// The quads live in m_Vertices (system memory), so unlike the Windows
	// path there is no vertex buffer to (re)create here.
	m_iPrimitiveCount = 0;
	return S_OK;
}

HRESULT CDFont::InvalidateDeviceObjects()
{
	m_Vertices.clear();
	m_iPrimitiveCount = 0;
	return S_OK;
}

HRESULT CDFont::DeleteDeviceObjects()
{
	if (m_pTexture != nullptr)
	{
		m_pTexture->Release();
		m_pTexture = nullptr;
	}
	m_pd3dDevice = nullptr;
	return S_OK;
}

HRESULT CDFont::SetText(const std::string& szText, uint32_t dwFlags)
{
	if (szText.empty())
	{
		m_iPrimitiveCount = 0;
		m_Vertices.clear();
		if (m_pTexture != nullptr)
		{
			m_pTexture->Release();
			m_pTexture = nullptr;
		}
		return S_OK;
	}

	FT_Face pFace = SizedFace(m_dwFontHeight);
	if (pFace == nullptr || RHIDevice() == nullptr)
		return E_FAIL;

	const std::u32string codepoints = DecodeGameText(szText);
	if (codepoints.empty())
		return E_FAIL;

	// Texture size selection - same heuristic as the GDI path: smallest square
	// from 32..2048 whose area fits the one-line extent with a safety margin
	// of half a CJK cell plus one line.
	const SIZE size = MeasureStripped(pFace, codepoints);
	if (size.cx <= 0 || size.cy <= 0)
		return E_FAIL;

	const int iExtent  = size.cx * size.cy;

	int iHalfCJK       = GlyphAdvance(pFace, U'진');
	if (iHalfCJK <= 0)
		iHalfCJK = size.cy;
	iHalfCJK           = iHalfCJK / 2 + (iHalfCJK % 2);

	const int iTexSizes[7] = { 32, 64, 128, 256, 512, 1024, 2048 };
	m_dwTexWidth = m_dwTexHeight = iTexSizes[6];
	for (const int iTexSize : iTexSizes)
	{
		if (iExtent <= (iTexSize - iHalfCJK - size.cy - 1) * iTexSize)
		{
			m_dwTexWidth = m_dwTexHeight = iTexSize;
			break;
		}
	}
	m_fTextScale = 1.0f; // s_DevCaps.MaxTextureWidth (4096) always fits 2048

	// Recreate the texture only when the required size changed.
	if (m_pTexture != nullptr)
	{
		D3DSURFACE_DESC sd {};
		m_pTexture->GetLevelDesc(0, &sd);
		if (sd.Width != m_dwTexWidth)
		{
			m_pTexture->Release();
			m_pTexture = nullptr;
		}
	}

	if (m_pTexture == nullptr)
	{
		const HRESULT hr = RHIDevice()->CreateTexture(m_dwTexWidth, m_dwTexHeight, 1, 0,
			D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &m_pTexture);
		if (FAILED(hr))
			return hr;
	}

	// Layout + rasterize in one pass. The pen (x, y) walks the texture as a
	// running strip that wraps at the right edge; the quad cursor (vtx_sx,
	// vtx_sy) walks the screen, advancing lines only at '\n'. Identical to
	// the GDI Make2DVertex flow.
	const int iLineHeight = size.cy;
	const int iAscender   = static_cast<int>((pFace->size->metrics.ascender + 63) >> 6);

	std::vector<uint8_t> alphaMap(static_cast<size_t>(m_dwTexWidth) * m_dwTexHeight, 0);

	m_Vertices.clear();
	uint32_t dwNumTriangles = 0;
	uint32_t sx = 0, x = 0, y = 0;
	float vtx_sx = 0.0f, vtx_sy = 0.0f;
	float fMaxX = 0.0f, fMaxY = 0.0f;
	const uint32_t dwColor = 0xffffffff;
	m_dwFontColor          = dwColor;

	// Emits the quad for the [sx, x) run of the current texture row and
	// returns its screen width.
	const auto EmitRun     = [&]() -> float {
		if (sx == x)
			return 0.0f;

		const float tx1     = static_cast<float>(sx) / m_dwTexWidth;
		const float ty1     = static_cast<float>(y) / m_dwTexHeight;
		const float tx2     = static_cast<float>(x) / m_dwTexWidth;
		const float ty2     = static_cast<float>(y + iLineHeight) / m_dwTexHeight;

		const float w       = (tx2 - tx1) * m_dwTexWidth;
		const float h       = (ty2 - ty1) * m_dwTexHeight;

		// D3D9-only half-pixel offset (see IRHIDevice::NeedsHalfPixelOffset).
		const float fHP     = (RHIDevice() != nullptr && RHIDevice()->NeedsHalfPixelOffset()) ? 0.5f : 0.0f;
		const float fLeft   = vtx_sx + 0 - fHP;
		const float fRight  = vtx_sx + w - fHP;
		const float fTop    = vtx_sy + 0 - fHP;
		const float fBottom = vtx_sy + h - fHP;

		__VertexTransformed v {};
		v.Set(fLeft, fBottom, Z_DEFAULT, RHW_DEFAULT, dwColor, tx1, ty2);
		m_Vertices.push_back(v);
		v.Set(fLeft, fTop, Z_DEFAULT, RHW_DEFAULT, dwColor, tx1, ty1);
		m_Vertices.push_back(v);
		v.Set(fRight, fBottom, Z_DEFAULT, RHW_DEFAULT, dwColor, tx2, ty2);
		m_Vertices.push_back(v);
		v.Set(fRight, fTop, Z_DEFAULT, RHW_DEFAULT, dwColor, tx2, ty1);
		m_Vertices.push_back(v);
		v.Set(fRight, fBottom, Z_DEFAULT, RHW_DEFAULT, dwColor, tx2, ty2);
		m_Vertices.push_back(v);
		v.Set(fLeft, fTop, Z_DEFAULT, RHW_DEFAULT, dwColor, tx1, ty1);
		m_Vertices.push_back(v);

		dwNumTriangles += 2;
		fMaxX           = std::max(fMaxX, fRight);
		fMaxY           = std::max(fMaxY, fBottom);
		return w;
	};

	for (const char32_t cp : codepoints)
	{
		if (cp == U'\n')
		{
			EmitRun();
			sx     = x;
			vtx_sx = 0.0f;
			vtx_sy += static_cast<float>(iLineHeight);
			continue;
		}

		const int iAdvance = GlyphAdvance(pFace, cp);
		if (x + iAdvance > m_dwTexWidth)
		{
			// Close the run and wrap the pen to the next texture row; the
			// text continues on the same visual line.
			if (sx != x)
				vtx_sx += EmitRun();
			x  = sx = 0;
			y += iLineHeight;
			if (y + iLineHeight > m_dwTexHeight)
				break; // texture full; clip the rest (GDI clipped likewise)
		}

		// Rasterize the glyph at the pen, clipped to the texture.
		// Unhinted rendering: keeps the glyph's true outline (macOS-style
		// smoothing) instead of snapping stems to the pixel grid.
		if (FT_Load_Char(pFace, cp, FT_LOAD_RENDER | FT_LOAD_NO_HINTING) == 0)
		{
			const FT_GlyphSlot pGlyph = pFace->glyph;
			const FT_Bitmap& bitmap   = pGlyph->bitmap;
			const int iLeft           = static_cast<int>(x) + pGlyph->bitmap_left;
			const int iTop            = static_cast<int>(y) + iAscender - pGlyph->bitmap_top;

			for (unsigned int row = 0; row < bitmap.rows; ++row)
			{
				const int iDstY = iTop + static_cast<int>(row);
				if (iDstY < 0 || iDstY >= static_cast<int>(m_dwTexHeight))
					continue;
				for (unsigned int col = 0; col < bitmap.width; ++col)
				{
					const int iDstX = iLeft + static_cast<int>(col);
					if (iDstX < 0 || iDstX >= static_cast<int>(m_dwTexWidth))
						continue;
					const uint8_t byAlpha = bitmap.buffer[row * bitmap.pitch + col];
					uint8_t& byDst = alphaMap[static_cast<size_t>(iDstY) * m_dwTexWidth + iDstX];
					byDst          = std::max(byDst, byAlpha);
				}
			}
		}

		x += iAdvance;
	}
	EmitRun();

	// Upload: full 8-bit alpha, white RGB (A8R8G8B8). The historical GDI path
	// packed A4R4G4B4 (16 alpha levels), which visibly banded the AA edges.
	D3DLOCKED_RECT lr {};
	if (SUCCEEDED(m_pTexture->LockRect(0, &lr, nullptr, 0)))
	{
		for (uint32_t row = 0; row < m_dwTexHeight; ++row)
		{
			auto* pDst32 = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lr.pBits)
													   + static_cast<size_t>(row) * lr.Pitch);
			for (uint32_t col = 0; col < m_dwTexWidth; ++col)
			{
				const uint8_t byAlpha = alphaMap[static_cast<size_t>(row) * m_dwTexWidth + col];
				pDst32[col] = (byAlpha > 0)
								  ? ((static_cast<uint32_t>(byAlpha) << 24) | 0x00ffffffu)
								  : 0u;
			}
		}
		m_pTexture->UnlockRect(0);
	}

	(void) dwFlags; // FILTERED only changes the sampler filter in DrawText

	m_iPrimitiveCount = dwNumTriangles;
	m_PrevLeftTop     = {};
	m_Size.cx         = static_cast<long>(fMaxX);
	m_Size.cy         = static_cast<long>(fMaxY);

	return S_OK;
}

void CDFont::Make2DVertex(const int /*iFontHeight*/, const std::string& /*szText*/)
{
	// Folded into SetText on POSIX (layout and rasterization share one pass).
}

HRESULT CDFont::DrawText(FLOAT sx, FLOAT sy, uint32_t dwColor, uint32_t dwFlags)
{
	if (m_iPrimitiveCount <= 0)
		return S_OK;

	IRHIDevice* pDevice = RHIDevice();
	if (pDevice == nullptr || m_pTexture == nullptr || m_Vertices.empty())
		return E_FAIL;

	// Translate/tint the cached quads only when something changed.
	const __Vector2 vDiff = __Vector2(sx, sy) - m_PrevLeftTop;
	if (fabs(vDiff.x) > 0.5f || fabs(vDiff.y) > 0.5f || dwColor != m_dwFontColor)
	{
		if (fabs(vDiff.x) > 0.5f)
		{
			m_PrevLeftTop.x = sx;
			for (__VertexTransformed& v : m_Vertices)
				v.x += vDiff.x;
		}

		if (fabs(vDiff.y) > 0.5f)
		{
			m_PrevLeftTop.y = sy;
			for (__VertexTransformed& v : m_Vertices)
				v.y += vDiff.y;
		}

		if (dwColor != m_dwFontColor)
		{
			m_dwFontColor = dwColor;
			for (__VertexTransformed& v : m_Vertices)
				v.color = m_dwFontColor;
		}
	}

	// Back up, set, draw, restore - the same render-state footprint as the
	// Windows DrawText (alpha blend over, no Z, no fog, modulate stage 0).
	DWORD dwAlphaBlend = 0, dwSrcBlend = 0, dwDestBlend = 0, dwZEnable = 0, dwFog = 0;
	DWORD dwColorOp = 0, dwColorArg1 = 0, dwColorArg2 = 0, dwAlphaOp = 0, dwAlphaArg1 = 0,
		  dwAlphaArg2 = 0, dwMinFilter = 0, dwMagFilter = 0;

	pDevice->GetRenderState(D3DRS_ALPHABLENDENABLE, &dwAlphaBlend);
	pDevice->GetRenderState(D3DRS_SRCBLEND, &dwSrcBlend);
	pDevice->GetRenderState(D3DRS_DESTBLEND, &dwDestBlend);
	pDevice->GetRenderState(D3DRS_ZENABLE, &dwZEnable);
	pDevice->GetRenderState(D3DRS_FOGENABLE, &dwFog);

	pDevice->GetTextureStageState(0, D3DTSS_COLOROP, &dwColorOp);
	pDevice->GetTextureStageState(0, D3DTSS_COLORARG1, &dwColorArg1);
	pDevice->GetTextureStageState(0, D3DTSS_COLORARG2, &dwColorArg2);
	pDevice->GetTextureStageState(0, D3DTSS_ALPHAOP, &dwAlphaOp);
	pDevice->GetTextureStageState(0, D3DTSS_ALPHAARG1, &dwAlphaArg1);
	pDevice->GetTextureStageState(0, D3DTSS_ALPHAARG2, &dwAlphaArg2);
	pDevice->GetSamplerState(0, D3DSAMP_MINFILTER, &dwMinFilter);
	pDevice->GetSamplerState(0, D3DSAMP_MAGFILTER, &dwMagFilter);

	pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	pDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
	pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	pDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

	const DWORD dwFilter = (dwFlags & D3DFONT_FILTERED) ? D3DTEXF_LINEAR : D3DTEXF_POINT;
	pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, dwFilter);
	pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, dwFilter);

	pDevice->SetFVF(FVF_TRANSFORMED);
	pDevice->SetTexture(0, m_pTexture);
	pDevice->DrawPrimitiveUP(
		D3DPT_TRIANGLELIST, m_iPrimitiveCount, m_Vertices.data(), sizeof(__VertexTransformed));

	pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, dwAlphaBlend);
	pDevice->SetRenderState(D3DRS_SRCBLEND, dwSrcBlend);
	pDevice->SetRenderState(D3DRS_DESTBLEND, dwDestBlend);
	pDevice->SetRenderState(D3DRS_ZENABLE, dwZEnable);
	pDevice->SetRenderState(D3DRS_FOGENABLE, dwFog);
	pDevice->SetTextureStageState(0, D3DTSS_COLOROP, dwColorOp);
	pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, dwColorArg1);
	pDevice->SetTextureStageState(0, D3DTSS_COLORARG2, dwColorArg2);
	pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, dwAlphaOp);
	pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, dwAlphaArg1);
	pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, dwAlphaArg2);
	pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, dwMinFilter);
	pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, dwMagFilter);

	return S_OK;
}

BOOL CDFont::GetTextExtent(const std::string& szString, int iStrLen, SIZE* pSize)
{
	if (pSize == nullptr)
		return FALSE;

	FT_Face pFace = SizedFace(m_dwFontHeight);
	if (pFace == nullptr)
	{
		// No font: report zero width so word-wrap degrades like the T6.8 stub.
		pSize->cx = 0;
		pSize->cy = static_cast<LONG>(m_dwFontHeight);
		return TRUE;
	}

	if (iStrLen < 0 || iStrLen > static_cast<int>(szString.size()))
		iStrLen = static_cast<int>(szString.size());

	const std::u32string codepoints =
		DecodeGameText(std::string_view(szString).substr(0, static_cast<size_t>(iStrLen)));
	*pSize = MeasureStripped(pFace, codepoints);
	return TRUE;
}

HRESULT CDFont::SetFontColor(uint32_t dwColor)
{
	if (m_iPrimitiveCount <= 0 || m_Vertices.empty())
		return E_FAIL;

	if (dwColor == m_dwFontColor)
		return S_OK;

	m_dwFontColor = dwColor;
	for (__VertexTransformed& v : m_Vertices)
		v.color = m_dwFontColor;

	return S_OK;
}

#endif // _WIN32
