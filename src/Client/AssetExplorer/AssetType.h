#ifndef CLIENT_ASSETEXPLORER_ASSETTYPE_H
#define CLIENT_ASSETEXPLORER_ASSETTYPE_H

#pragma once

// Asset classification for the Asset Explorer (docs/ASSET_EXPLORER_PLAN.md, M0).
//
// Two levels: a fine-grained AssetType (one per N3 loader / file kind) used by
// the inspector and loader facade, and a coarse AssetCategory used by the
// filter chips in the UI. Classification is display-only metadata; nothing here
// reads or decodes the file contents, so it is trivially unit-testable without a
// game-data tree (tests/AssetExplorer/AssetType_test.cpp).

#include <string>
#include <string_view>

namespace assetexplorer
{

// One entry per distinct on-disk asset kind the engine can load. Extensions in
// parentheses are the real ones observed across the client/world loaders.
enum class AssetType
{
	Unknown,          // unrecognized / non-asset file
	Texture,          // .dxt .tga .n3tex .bmp  (CN3Texture)
	EncryptedTexture, // .ksc                   (JPEG behind the Borland cipher)
	Shape,            // .n3shape               (CN3Shape: PMesh instance + textures)
	Mesh,             // .n3pmesh .n3vmesh      (CN3PMesh / CN3VMesh)
	Character,        // .n3chr                 (CN3Chr: skeleton + skins + anims)
	CharacterPart,    // .n3cpart .n3cplug      (CN3CPart body part / CN3CPlug plug)
	Animation,        // .n3anim                (CN3AnimControl keys)
	Effect,           // .n3fxbundle            (CN3FXBundle)
	Terrain,          // .gtd                   (CN3Terrain game-terrain data)
};

// Coarse grouping that drives the UI filter chips (Tex / Model / Chr / FX / Map).
enum class AssetCategory
{
	Other,
	Texture,
	Model,
	Character,
	Effect,
	Map,
};

/// Classify a path by its file extension (case-insensitive). Never touches the
/// filesystem or the file contents.
AssetType DetectByExtension(std::string_view path);

/// Coarse category a fine-grained type belongs to (for the filter chips).
AssetCategory CategoryOf(AssetType type);

/// Human-readable full name, e.g. "Character", "Encrypted texture".
const char* AssetTypeName(AssetType type);

/// Short chip label for a category, e.g. "Tex", "Model", "Chr", "FX", "Map".
const char* AssetCategoryLabel(AssetCategory category);

/// True for types the explorer can meaningfully preview today (everything
/// except Unknown). Kept as a helper so the UI can grey out the rest.
bool IsPreviewable(AssetType type);

} // namespace assetexplorer

#endif // CLIENT_ASSETEXPLORER_ASSETTYPE_H
