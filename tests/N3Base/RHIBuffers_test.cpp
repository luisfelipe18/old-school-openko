#include <gtest/gtest.h>

#include <N3Base/RHI/RHIDeviceNull.h>

#include <cstring>

TEST(RHIBuffersTest, CreateVertexBufferAllocatesRealStorage)
{
	RHIDeviceNull device;

	IRHIVertexBuffer* pVB = nullptr;
	ASSERT_EQ(device.CreateVertexBuffer(64, 0, FVF_XYZCOLOR, D3DPOOL_MANAGED, &pVB), D3D_OK);
	ASSERT_NE(pVB, nullptr);
	EXPECT_EQ(pVB->Length(), 64u);
	EXPECT_EQ(pVB->FVF(), static_cast<DWORD>(FVF_XYZCOLOR));

	// Write through one Lock, read back through another - the storage is real.
	float* pData = nullptr;
	ASSERT_EQ(pVB->Lock(0, 0, (void**) &pData, 0), D3D_OK);
	ASSERT_NE(pData, nullptr);
	for (int i = 0; i < 16; ++i)
		pData[i] = float(i);
	EXPECT_EQ(pVB->Unlock(), D3D_OK);

	pData = nullptr;
	ASSERT_EQ(pVB->Lock(0, 0, (void**) &pData, 0), D3D_OK);
	for (int i = 0; i < 16; ++i)
		EXPECT_EQ(pData[i], float(i));
	EXPECT_EQ(pVB->Unlock(), D3D_OK);

	EXPECT_EQ(pVB->Release(), 0u);
}

TEST(RHIBuffersTest, LockRespectsOffsetAndRange)
{
	RHIDeviceNull device;

	IRHIVertexBuffer* pVB = nullptr;
	ASSERT_EQ(device.CreateVertexBuffer(16, 0, 0, D3DPOOL_MANAGED, &pVB), D3D_OK);

	uint8_t* pBase = nullptr;
	ASSERT_EQ(pVB->Lock(0, 16, (void**) &pBase, 0), D3D_OK);
	EXPECT_EQ(pVB->Unlock(), D3D_OK);

	// D3D9 semantics: size 0 locks from the offset to the end.
	uint8_t* pTail = nullptr;
	ASSERT_EQ(pVB->Lock(8, 0, (void**) &pTail, 0), D3D_OK);
	EXPECT_EQ(pTail, pBase + 8);
	EXPECT_EQ(pVB->Unlock(), D3D_OK);

	// Out-of-range locks fail instead of handing out invalid memory.
	void* pData = nullptr;
	EXPECT_NE(pVB->Lock(16, 0, &pData, 0), D3D_OK);
	EXPECT_NE(pVB->Lock(8, 9, &pData, 0), D3D_OK);
	EXPECT_NE(pVB->Lock(0, 0, nullptr, 0), D3D_OK);

	EXPECT_EQ(pVB->Release(), 0u);
}

TEST(RHIBuffersTest, CreateRejectsBadArguments)
{
	RHIDeviceNull device;

	IRHIVertexBuffer* pVB = nullptr;
	EXPECT_NE(device.CreateVertexBuffer(0, 0, 0, D3DPOOL_MANAGED, &pVB), D3D_OK);
	EXPECT_NE(device.CreateVertexBuffer(16, 0, 0, D3DPOOL_MANAGED, nullptr), D3D_OK);

	IRHIIndexBuffer* pIB = nullptr;
	EXPECT_NE(device.CreateIndexBuffer(0, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &pIB), D3D_OK);
	EXPECT_NE(device.CreateIndexBuffer(16, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, nullptr), D3D_OK);
}

TEST(RHIBuffersTest, IndexBufferRoundTrip)
{
	RHIDeviceNull device;

	IRHIIndexBuffer* pIB = nullptr;
	ASSERT_EQ(device.CreateIndexBuffer(
				  6 * sizeof(uint16_t), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &pIB),
		D3D_OK);
	ASSERT_NE(pIB, nullptr);
	EXPECT_EQ(pIB->Length(), 6u * sizeof(uint16_t));
	EXPECT_EQ(pIB->Format(), D3DFMT_INDEX16);

	const uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
	uint16_t* pData           = nullptr;
	ASSERT_EQ(pIB->Lock(0, sizeof(indices), (void**) &pData, 0), D3D_OK);
	memcpy(pData, indices, sizeof(indices));
	EXPECT_EQ(pIB->Unlock(), D3D_OK);

	pData = nullptr;
	ASSERT_EQ(pIB->Lock(0, 0, (void**) &pData, 0), D3D_OK);
	EXPECT_EQ(memcmp(pData, indices, sizeof(indices)), 0);
	EXPECT_EQ(pIB->Unlock(), D3D_OK);

	EXPECT_EQ(pIB->Release(), 0u);
}

TEST(RHIBuffersTest, StreamSourceAndIndicesBind)
{
	RHIDeviceNull device;

	IRHIVertexBuffer* pVB = nullptr;
	IRHIIndexBuffer* pIB  = nullptr;
	ASSERT_EQ(device.CreateVertexBuffer(32, 0, 0, D3DPOOL_MANAGED, &pVB), D3D_OK);
	ASSERT_EQ(device.CreateIndexBuffer(32, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &pIB), D3D_OK);

	EXPECT_EQ(device.SetStreamSource(0, pVB, 0, 16), D3D_OK);
	EXPECT_EQ(device.SetIndices(pIB), D3D_OK);
	EXPECT_EQ(device.BoundVertexBuffer(), pVB);
	EXPECT_EQ(device.BoundIndexBuffer(), pIB);

	// Unbinding with nullptr is legal (the engine does it when restoring state).
	EXPECT_EQ(device.SetStreamSource(0, nullptr, 0, 0), D3D_OK);
	EXPECT_EQ(device.SetIndices(nullptr), D3D_OK);
	EXPECT_EQ(device.BoundVertexBuffer(), nullptr);
	EXPECT_EQ(device.BoundIndexBuffer(), nullptr);

	EXPECT_EQ(pVB->Release(), 0u);
	EXPECT_EQ(pIB->Release(), 0u);
}
