#include <gtest/gtest.h>

#include <AssetType.h>

using namespace assetexplorer;

TEST(AssetType, DetectsByExtensionCaseInsensitive)
{
	EXPECT_EQ(DetectByExtension("mob/orc.n3chr"), AssetType::Character);
	EXPECT_EQ(DetectByExtension("MOB/ORC.N3CHR"), AssetType::Character);
	EXPECT_EQ(DetectByExtension("item/sword.n3cplug"), AssetType::CharacterPart);
	EXPECT_EQ(DetectByExtension("tile/grass.dxt"), AssetType::Texture);
	EXPECT_EQ(DetectByExtension("ui/logo.tga"), AssetType::Texture);
	EXPECT_EQ(DetectByExtension("login/bg.ksc"), AssetType::EncryptedTexture);
	EXPECT_EQ(DetectByExtension("obj/house.n3shape"), AssetType::Shape);
	EXPECT_EQ(DetectByExtension("obj/house.n3pmesh"), AssetType::Mesh);
	EXPECT_EQ(DetectByExtension("fx/fireball.n3fxbundle"), AssetType::Effect);
	EXPECT_EQ(DetectByExtension("zone/karus.gtd"), AssetType::Terrain);
	EXPECT_EQ(DetectByExtension("walk.n3anim"), AssetType::Animation);
}

TEST(AssetType, UnknownAndEdgeCases)
{
	EXPECT_EQ(DetectByExtension("readme.txt"), AssetType::Unknown);
	EXPECT_EQ(DetectByExtension("noextension"), AssetType::Unknown);
	EXPECT_EQ(DetectByExtension(""), AssetType::Unknown);
	// A dot in a parent directory must not be read as the file's extension.
	EXPECT_EQ(DetectByExtension("a.n3chr/plainfile"), AssetType::Unknown);
	// Trailing dot has no extension.
	EXPECT_EQ(DetectByExtension("foo."), AssetType::Unknown);
}

TEST(AssetType, CategoryGrouping)
{
	EXPECT_EQ(CategoryOf(AssetType::Texture), AssetCategory::Texture);
	EXPECT_EQ(CategoryOf(AssetType::EncryptedTexture), AssetCategory::Texture);
	EXPECT_EQ(CategoryOf(AssetType::Shape), AssetCategory::Model);
	EXPECT_EQ(CategoryOf(AssetType::Mesh), AssetCategory::Model);
	EXPECT_EQ(CategoryOf(AssetType::Character), AssetCategory::Character);
	EXPECT_EQ(CategoryOf(AssetType::CharacterPart), AssetCategory::Character);
	EXPECT_EQ(CategoryOf(AssetType::Animation), AssetCategory::Character);
	EXPECT_EQ(CategoryOf(AssetType::Effect), AssetCategory::Effect);
	EXPECT_EQ(CategoryOf(AssetType::Terrain), AssetCategory::Map);
	EXPECT_EQ(CategoryOf(AssetType::Unknown), AssetCategory::Other);
}

TEST(AssetType, LabelsAndPreviewable)
{
	EXPECT_STREQ(AssetCategoryLabel(AssetCategory::Texture), "Tex");
	EXPECT_STREQ(AssetCategoryLabel(AssetCategory::Model), "Model");
	EXPECT_STREQ(AssetCategoryLabel(AssetCategory::Character), "Chr");
	EXPECT_STREQ(AssetCategoryLabel(AssetCategory::Effect), "FX");
	EXPECT_STREQ(AssetCategoryLabel(AssetCategory::Map), "Map");

	EXPECT_TRUE(IsPreviewable(AssetType::Character));
	EXPECT_FALSE(IsPreviewable(AssetType::Unknown));
}
