#include "pch.h"
#include "JvCryption.h"
#include "version.h"

#include <cstring> // memcpy

// Cryption
constexpr uint64_t g_private_key = 0x1234567890123456;

void CJvCryption::Init()
{
	m_tkey = m_public_key ^ g_private_key;
}

uint64_t CJvCryption::GenerateKey()
{
#ifdef USE_CRYPTION
	// because of their sucky encryption method, 0 means it effectively won't be encrypted.
	// We don't want that happening...
	do
	{
		// NOTE: Debugging
		m_public_key = RandUInt64(); //0xDCE04F8975278163;
	}
	while (m_public_key == 0);

	Init();
#endif

	return m_public_key;
}

void CJvCryption::JvEncryptionFast(int len, uint8_t* datain, uint8_t* dataout)
{
#ifdef USE_CRYPTION
	const uint8_t* pkey = reinterpret_cast<const uint8_t*>(&m_tkey);
	uint8_t lkey = 0, rsk = 0;
	// The keystream is a multiplicative generator that deliberately relies on
	// modulo-2^32 wraparound. Signed overflow is UB (UBSan flags it and it can
	// misbehave under aggressive optimizers), so keep the accumulator unsigned:
	// the low bytes used below (>>8 & 0xff) are bit-identical to the historical
	// signed-int behaviour, so the wire encryption is unchanged.
	uint32_t rkey = 2157;

	lkey          = (len * 157) & 0xff;

	for (int i = 0; i < len; i++)
	{
		rsk         = (rkey >> 8) & 0xff;
		dataout[i]  = ((datain[i] ^ rsk) ^ pkey[(i % 8)]) ^ lkey;
		rkey       *= 2171;
	}

	return;
#endif

	dataout = datain;
}

int CJvCryption::JvDecryptionWithCRC32(int len, uint8_t* datain, uint8_t* dataout)
{
#ifdef USE_CRYPTION
	int result = -1;
	JvDecryptionFast(len, datain, dataout);

	// dataout + len - 4 isn't guaranteed to be 4-byte aligned (len comes from
	// the wire), so read the trailing CRC via memcpy rather than a raw
	// pointer-cast dereference (misaligned-load UB).
	uint32_t storedCrc = 0;
	memcpy(&storedCrc, dataout + len - 4, sizeof(storedCrc));

	if (crc32(dataout, len - 4, -1) == storedCrc)
		result = len - 4;

	return result;
#else
	dataout = datain;
	return (len - 4);
#endif
}
