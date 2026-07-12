#include "AssetType.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace assetexplorer
{
namespace
{

// Lowercased extension (without the dot) of a path, e.g. "foo/BAR.N3Chr" -> "n3chr".
// Returns empty if there is no extension after the last path separator.
std::string LowerExtension(std::string_view path)
{
	// Find the basename first so a dot in a parent directory can't be mistaken
	// for an extension ("a.b/c" has no extension).
	const std::size_t slash = path.find_last_of("/\\");
	const std::string_view name =
		(slash == std::string_view::npos) ? path : path.substr(slash + 1);

	const std::size_t dot = name.find_last_of('.');
	if (dot == std::string_view::npos || dot + 1 >= name.size())
		return {};

	std::string ext(name.substr(dot + 1));
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext;
}

struct ExtMapping
{
	std::string_view ext;
	AssetType type;
};

// Extension -> type table. Extensions come from the real loaders (see the
// header). Ordering is irrelevant; lookup is a linear scan (tiny table).
constexpr std::array<ExtMapping, 15> kExtTable{{
	{"dxt", AssetType::Texture},
	{"tga", AssetType::Texture},
	{"n3tex", AssetType::Texture},
	{"bmp", AssetType::Texture},
	{"ksc", AssetType::EncryptedTexture},
	{"n3shape", AssetType::Shape},
	{"n3pmesh", AssetType::Mesh},
	{"n3vmesh", AssetType::Mesh},
	{"n3chr", AssetType::Character},
	{"n3cpart", AssetType::CharacterPart},
	{"n3cplug", AssetType::CharacterPart},
	{"n3anim", AssetType::Animation},
	{"n3fxbundle", AssetType::Effect},
	{"n3fxb", AssetType::Effect},
	{"gtd", AssetType::Terrain},
}};

} // namespace

AssetType DetectByExtension(std::string_view path)
{
	const std::string ext = LowerExtension(path);
	if (ext.empty())
		return AssetType::Unknown;

	for (const ExtMapping& m : kExtTable)
	{
		if (ext == m.ext)
			return m.type;
	}
	return AssetType::Unknown;
}

AssetCategory CategoryOf(AssetType type)
{
	switch (type)
	{
		case AssetType::Texture:
		case AssetType::EncryptedTexture:
			return AssetCategory::Texture;
		case AssetType::Shape:
		case AssetType::Mesh:
			return AssetCategory::Model;
		case AssetType::Character:
		case AssetType::CharacterPart:
		case AssetType::Animation:
			return AssetCategory::Character;
		case AssetType::Effect:
			return AssetCategory::Effect;
		case AssetType::Terrain:
			return AssetCategory::Map;
		case AssetType::Unknown:
			break;
	}
	return AssetCategory::Other;
}

const char* AssetTypeName(AssetType type)
{
	switch (type)
	{
		case AssetType::Texture:          return "Texture";
		case AssetType::EncryptedTexture: return "Encrypted texture";
		case AssetType::Shape:            return "Shape";
		case AssetType::Mesh:             return "Mesh";
		case AssetType::Character:        return "Character";
		case AssetType::CharacterPart:    return "Character part";
		case AssetType::Animation:        return "Animation";
		case AssetType::Effect:           return "Effect";
		case AssetType::Terrain:          return "Terrain";
		case AssetType::Unknown:          break;
	}
	return "Unknown";
}

const char* AssetCategoryLabel(AssetCategory category)
{
	switch (category)
	{
		case AssetCategory::Texture:   return "Tex";
		case AssetCategory::Model:     return "Model";
		case AssetCategory::Character: return "Chr";
		case AssetCategory::Effect:    return "FX";
		case AssetCategory::Map:       return "Map";
		case AssetCategory::Other:     break;
	}
	return "Other";
}

bool IsPreviewable(AssetType type)
{
	return type != AssetType::Unknown;
}

} // namespace assetexplorer
