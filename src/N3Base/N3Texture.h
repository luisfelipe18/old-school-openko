#ifndef N3BASE_N3TEXTURE_H
#define N3BASE_N3TEXTURE_H

#pragma once

#include "N3BaseFileAccess.h"
#include "RHI/RHIDevice.h"
#include <string>

class CN3Texture : public CN3BaseFileAccess
{
public:
	typedef struct __DXT_HEADER
	{
		char szID[4];     // "NTF"숫자 - Noah Texture File Ver. ?.0
		int nWidth;
		int nHeight;
		D3DFORMAT Format; // 0 - 압축 안함 1 ~ 5 : D3DFMT_DXT1 ~ D3DFMT_DXT5
		BOOL bMipMap;     // Mip Map ??
	} __DxtHeader;

protected:
	__DXT_HEADER m_Header;
	IRHITexture* m_lpTexture;

public:
	void UpdateRenderInfo();
	bool LoadFromFile(const std::string& szFileName) override;
	bool Load(File& file) override;
	bool SkipFileHandle(File& file);

#ifdef _N3TOOL
	bool GenerateMipMap(
		LPDIRECT3DSURFACE9 lpSurf = nullptr); // nullptr 이면 0 레벨의 서피스로부터 생성..
	bool Convert(D3DFORMAT Format, int nWidth = 0, int nHeight = 0, BOOL bGenerateMipMap = TRUE);
	//#ifdef _N3TOOL
	bool SaveToFile() override;                              // 현재 파일 이름대로 저장.
	bool SaveToFile(const std::string& szFileName) override; // 새이름으로 저장.
	bool Save(File& file) override;
	bool SaveToBitmapFile(const std::string& szFN);          // 24비트 비트맵 파일로 저장..
	bool CreateFromSurface(LPDIRECT3DSURFACE9 lpSurf, D3DFORMAT Format, BOOL bGenerateMipMap);
#endif                                                       // end of _N3TOOL

	uint32_t Width()
	{
		return m_Header.nWidth;
	}
	uint32_t Height()
	{
		return m_Header.nHeight;
	}
	D3DFORMAT PixelFormat()
	{
		return m_Header.Format;
	}
	int MipMapCount()
	{
		if (nullptr == m_lpTexture)
			return 0;
		return m_lpTexture->GetLevelCount();
	}

	bool Create(
		int nWidth, int nHeight, D3DFORMAT Format, BOOL bGenerateMipMap); // 장치에 맞게 생성
	IRHITexture* Get()
	{
		return m_lpTexture;
	}
	operator IRHITexture*()
	{
		return m_lpTexture;
	}

#ifdef _WIN32
	// Raw D3D texture for the Windows-only editor/tool paths that still bind
	// through the D3D9 device (or D3DX) directly. Defined in N3Texture.cpp,
	// where the RHITextureD3D9 wrapper is visible.
	LPDIRECT3DTEXTURE9 GetRawD3D();
#endif

	void Release() override;
	CN3Texture();
	~CN3Texture() override;
};

#endif // N3BASE_N3TEXTURE_H
