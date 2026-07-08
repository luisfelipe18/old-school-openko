#ifndef N3BASE_WINCRYPT_H
#define N3BASE_WINCRYPT_H

#pragma once

#ifdef _WIN32
#include <wincrypt.h>
#else
#include <Platform/PlatformCrypto.h>
#endif

class File;
class CWinCrypt
{
public:
#ifdef _WIN32
	constexpr const static TCHAR Provider[] = MS_ENHANCED_PROV;
#endif
	constexpr const static char Cipher[]    = "owsd9012%$1as!wpow1033b%!@%12";

	inline bool IsLoaded() const
	{
		return m_bIsLoaded;
	}

	CWinCrypt();
	~CWinCrypt();
	bool Load();
	void Release();
	bool ReadFile(File& file, void* buffer, size_t bytesToRead, size_t* bytesRead = nullptr);

protected:
	bool m_bIsLoaded;
#ifdef _WIN32
	HCRYPTPROV m_hCryptProvider;
	HCRYPTHASH m_hCryptHash;
	HCRYPTKEY m_hCryptKey;
#else
	// CryptDeriveKey(CALG_RC4, SHA1(Cipher), 128-bit) equivalent: the RC4 key
	// is the leading 16 bytes of SHA1(Cipher).
	uint8_t m_byRc4Key[16];
#endif
};

#endif // #define N3BASE_WINCRYPT_H
