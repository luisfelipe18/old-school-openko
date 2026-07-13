#include <gtest/gtest.h>

#include <N3Base/RHI/RHIStateKey.h>

#include <unordered_map>

TEST(RHIStateKeyTest, EqualKeysHashEqually)
{
	RHIStateKey a;
	a.fvf              = 0x112; // FVF_VNT1
	a.primitiveType    = 4;     // D3DPT_TRIANGLELIST
	a.alphaBlendEnable = true;
	a.srcBlend         = 5;     // D3DBLEND_SRCALPHA
	a.destBlend        = 6;     // D3DBLEND_INVSRCALPHA

	RHIStateKey b = a;

	EXPECT_EQ(a, b);
	EXPECT_EQ(a.Hash(), b.Hash());
}

TEST(RHIStateKeyTest, DifferentStateProducesDifferentKeys)
{
	RHIStateKey opaque;
	opaque.fvf           = 0x112;
	opaque.primitiveType = 4;

	RHIStateKey blended  = opaque;
	blended.alphaBlendEnable = true;
	blended.srcBlend         = 5;
	blended.destBlend        = 6;

	RHIStateKey wireframe = opaque;
	wireframe.cullMode    = 1; // D3DCULL_NONE

	EXPECT_NE(opaque, blended);
	EXPECT_NE(opaque, wireframe);
	EXPECT_NE(blended, wireframe);

	// Not a guarantee in general, but these must differ for the cache to be
	// useful; a collision here means the hash is ignoring fields.
	EXPECT_NE(opaque.Hash(), blended.Hash());
	EXPECT_NE(opaque.Hash(), wireframe.Hash());
}

TEST(RHIStateKeyTest, WorksAsUnorderedMapKey)
{
	std::unordered_map<RHIStateKey, int, RHIStateKeyHasher> pipelineCache;

	RHIStateKey ui;
	ui.fvf              = 0x144; // FVF_TRANSFORMED
	ui.primitiveType    = 5;     // strips
	ui.alphaBlendEnable = true;

	RHIStateKey terrain;
	terrain.fvf           = 0x112;
	terrain.primitiveType = 4;
	terrain.zEnable       = true;
	terrain.zWriteEnable  = true;
	terrain.fogEnable     = true;

	pipelineCache[ui]      = 1;
	pipelineCache[terrain] = 2;

	EXPECT_EQ(pipelineCache.size(), 2u);
	EXPECT_EQ(pipelineCache[ui], 1);
	EXPECT_EQ(pipelineCache[terrain], 2);

	// Re-inserting an equal key hits the cache instead of growing it.
	RHIStateKey uiAgain = ui;
	pipelineCache[uiAgain] = 3;
	EXPECT_EQ(pipelineCache.size(), 2u);
}
