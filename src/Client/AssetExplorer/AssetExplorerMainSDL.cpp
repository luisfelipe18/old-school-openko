// POSIX entry point for the Asset Explorer tool
// (docs/ASSET_EXPLORER_PLAN.md, M0). A modern Dear ImGui app over the SDL3 +
// OpenGL stack the other client-tool ports use. This milestone (M0) delivers
// the shell: it discovers a game-data directory, indexes every asset by type,
// and presents a searchable/filterable three-pane layout (asset list, preview
// viewport placeholder, inspector). 3D/2D preview lands in M1+.
//
// A --smoke <dir> flag indexes a directory headlessly and prints the per-type
// counts for CI, without opening a window.

#include "AnimationPlayer.h"
#include "AssetIndex.h"
#include "AssetType.h"
#include "FlyCamera.h"
#include "OrbitCamera.h"

// CN3Terrain lives in the WarFare client, not N3Base.
#include <N3Terrain.h>

// Engine + RHI: the tool renders previews through the same GL backend the
// client uses, into an offscreen render target it then samples into the UI
// (docs/ASSET_EXPLORER_PLAN.md, M1).
#include <RHIDeviceGL.h>

#include <N3Base/My_3DStruct.h>
#include <N3Base/N3AnimControl.h>
#include <N3Base/N3Base.h>
#include <N3Base/N3Camera.h>
#include <N3Base/N3Chr.h>
#include <N3Base/N3FXBundle.h>
#include <N3Base/N3FXDef.h>
#include <N3Base/N3FXPartBase.h>
#include <N3Base/N3PMesh.h>
#include <N3Base/N3Shape.h>
#include <N3Base/N3Texture.h>

// KscViewer's platform-neutral decrypt/decode core, reused for .ksc textures.
#include <KscViewerCore.h>

#include <Platform/GameDataDir.h>
#include <Platform/IconDecoder.h>
#include <Platform/PlatformPaths.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <spdlog/spdlog.h>

#include <SDL3/SDL.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using namespace assetexplorer;

namespace
{

// The chip categories shown in the toolbar filter row, in display order.
constexpr std::array<AssetCategory, 5> kFilterChips{
	AssetCategory::Texture, AssetCategory::Model, AssetCategory::Character,
	AssetCategory::Effect, AssetCategory::Map};

// Same flat, rounded dark theme as the Option/Launcher/KscViewer ports so the
// tools look like a matched set (see OptionMainSDL.cpp for the rationale).
void ApplyModernStyle()
{
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();

	style.WindowRounding    = 8.0f;
	style.ChildRounding     = 6.0f;
	style.FrameRounding     = 5.0f;
	style.PopupRounding     = 6.0f;
	style.ScrollbarRounding = 8.0f;
	style.GrabRounding      = 5.0f;
	style.TabRounding       = 5.0f;

	style.WindowPadding    = ImVec2(16, 16);
	style.FramePadding     = ImVec2(10, 6);
	style.ItemSpacing      = ImVec2(10, 8);
	style.ItemInnerSpacing = ImVec2(8, 6);

	ImVec4* colors = style.Colors;
	colors[ImGuiCol_WindowBg]       = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
	colors[ImGuiCol_ChildBg]        = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
	colors[ImGuiCol_PopupBg]        = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
	colors[ImGuiCol_Border]         = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
	colors[ImGuiCol_FrameBg]        = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
	colors[ImGuiCol_FrameBgActive]  = ImVec4(0.24f, 0.28f, 0.36f, 1.00f);
	colors[ImGuiCol_TitleBg]        = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
	colors[ImGuiCol_TitleBgActive]  = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
	colors[ImGuiCol_Button]         = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
	colors[ImGuiCol_ButtonHovered]  = ImVec4(0.28f, 0.45f, 0.68f, 1.00f);
	colors[ImGuiCol_ButtonActive]   = ImVec4(0.24f, 0.53f, 0.83f, 1.00f);
	colors[ImGuiCol_Header]         = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
	colors[ImGuiCol_HeaderHovered]  = ImVec4(0.28f, 0.45f, 0.68f, 1.00f);
	colors[ImGuiCol_HeaderActive]   = ImVec4(0.24f, 0.53f, 0.83f, 1.00f);
	colors[ImGuiCol_Separator]      = ImVec4(0.22f, 0.24f, 0.29f, 1.00f);
}

void ApplyWindowIcon(SDL_Window* pWindow)
{
	const fs::path iconPath = GetExecutableDir() / "AssetExplorer.ico";
	const DecodedIcon icon  = LoadIconFromFile(iconPath);
	if (!icon.IsValid())
		return; // best-effort, like the other tools

	SDL_Surface* pSurface = SDL_CreateSurfaceFrom(icon.width, icon.height,
		SDL_PIXELFORMAT_RGBA32, const_cast<uint8_t*>(icon.pixelsRgba.data()), icon.width * 4);
	if (pSurface != nullptr)
	{
		SDL_SetWindowIcon(pWindow, pSurface);
		SDL_DestroySurface(pSurface);
	}
}

std::string HumanSize(std::uintmax_t bytes)
{
	const char* units[] = {"B", "KB", "MB", "GB"};
	double v = static_cast<double>(bytes);
	int u = 0;
	while (v >= 1024.0 && u < 3)
	{
		v /= 1024.0;
		++u;
	}
	char buf[32];
	std::snprintf(buf, sizeof(buf), (u == 0) ? "%.0f %s" : "%.1f %s", v, units[u]);
	return buf;
}

// --- Texture preview (M2) ---------------------------------------------------

const char* D3DFormatName(D3DFORMAT fmt)
{
	switch (fmt)
	{
		case D3DFMT_DXT1:     return "DXT1";
		case D3DFMT_DXT2:     return "DXT2";
		case D3DFMT_DXT3:     return "DXT3";
		case D3DFMT_DXT4:     return "DXT4";
		case D3DFMT_DXT5:     return "DXT5";
		case D3DFMT_A8R8G8B8: return "A8R8G8B8";
		case D3DFMT_X8R8G8B8: return "X8R8G8B8";
		case D3DFMT_R5G6B5:   return "R5G6B5";
		case D3DFMT_A1R5G5B5: return "A1R5G5B5";
		case D3DFMT_A4R4G4B4: return "A4R4G4B4";
		default:              return "unknown";
	}
}

bool FormatHasAlpha(D3DFORMAT fmt)
{
	switch (fmt)
	{
		case D3DFMT_DXT2:
		case D3DFMT_DXT3:
		case D3DFMT_DXT4:
		case D3DFMT_DXT5:
		case D3DFMT_A8R8G8B8:
		case D3DFMT_A1R5G5B5:
		case D3DFMT_A4R4G4B4:
			return true;
		default:
			return false;
	}
}

// Everything needed to display + inspect + export the selected texture. For
// engine formats (.dxt/.tga) the pixels live in the CN3Texture and its GL
// object is shared for display; for .ksc a decoded RGB texture is owned here.
struct TexturePreview
{
	bool loaded = false;
	std::string error;

	std::unique_ptr<CN3Texture> n3tex; // engine texture (keeps GL object alive)
	unsigned glTexture = 0;            // display texture id
	bool ownGLTexture = false;         // true only for the .ksc path

	int width = 0;
	int height = 0;
	std::string formatName;
	int mipCount = 0;
	bool hasAlpha = false;
	bool isKsc = false;

	// view controls
	float zoom = 0.0f; // 0 == fit-to-window; otherwise pixel scale
	ImVec2 pan{0, 0};
	int background = 0; // 0 checker, 1 black, 2 white
};

void ReleaseTexturePreview(TexturePreview& tp)
{
	if (tp.ownGLTexture && tp.glTexture != 0)
	{
		GLuint t = tp.glTexture;
		glDeleteTextures(1, &t);
	}
	tp.n3tex.reset();
	tp = TexturePreview{};
}

unsigned UploadRgbTexture(const std::vector<uint8_t>& rgb, int w, int h)
{
	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
	glBindTexture(GL_TEXTURE_2D, 0);
	return tex;
}

// Loads the texture into `tp`. `relPath` is relative to the engine asset root
// (CN3Base::PathSet); `absPath` is the same file's absolute path for the
// filesystem-based .ksc decoder.
void LoadTexturePreview(TexturePreview& tp, AssetType type, const std::string& relPath,
	const fs::path& absPath)
{
	ReleaseTexturePreview(tp);

	if (type == AssetType::EncryptedTexture)
	{
		ksc_core::DecodedImage img = ksc_core::LoadImage(absPath);
		if (!img.IsValid())
		{
			tp.error = "Failed to decode " + absPath.filename().string();
			return;
		}
		tp.glTexture    = UploadRgbTexture(img.rgb, img.width, img.height);
		tp.ownGLTexture = true;
		tp.width        = img.width;
		tp.height       = img.height;
		tp.formatName   = "JPEG (KSC)";
		tp.mipCount     = 1;
		tp.hasAlpha     = false;
		tp.isKsc        = true;
		tp.loaded       = true;
		return;
	}

	// Engine formats (.dxt / .tga). CN3Texture auto-detects by content and
	// uploads through the RHI; the GL backend's texture object is what we show.
	auto n3 = std::make_unique<CN3Texture>();
	if (!n3->LoadFromFile(relPath) || n3->Get() == nullptr)
	{
		tp.error = "Failed to load " + absPath.filename().string();
		return;
	}
	auto* glTex   = static_cast<RHITextureGL*>(n3->Get());
	tp.glTexture  = glTex->GLTexture();
	tp.width      = static_cast<int>(n3->Width());
	tp.height     = static_cast<int>(n3->Height());
	tp.formatName = D3DFormatName(n3->PixelFormat());
	tp.mipCount   = n3->MipMapCount();
	tp.hasAlpha   = FormatHasAlpha(n3->PixelFormat());
	tp.n3tex      = std::move(n3);
	tp.loaded     = true;
}

// Writes a bottom-up 24-bit BMP from RGBA rows that glReadPixels returned
// (already bottom-up), swapping R/B into BGR. Returns false on I/O failure.
bool WriteBmp24(const fs::path& dst, const std::vector<uint8_t>& rgba, int w, int h)
{
	const int rowBytes  = w * 3;
	const int padding   = (4 - (rowBytes % 4)) % 4;
	const int imageSize = (rowBytes + padding) * h;
	const int fileSize  = 54 + imageSize;

	std::ofstream out(dst, std::ios::binary);
	if (!out)
		return false;

	auto u16 = [&](int v) { char b[2] = {char(v & 0xFF), char((v >> 8) & 0xFF)}; out.write(b, 2); };
	auto u32 = [&](int v) {
		char b[4] = {char(v & 0xFF), char((v >> 8) & 0xFF), char((v >> 16) & 0xFF),
			char((v >> 24) & 0xFF)};
		out.write(b, 4);
	};

	out.write("BM", 2);
	u32(fileSize);
	u32(0);
	u32(54); // pixel data offset
	u32(40); // DIB header size
	u32(w);
	u32(h);
	u16(1);  // planes
	u16(24); // bpp
	u32(0);  // no compression
	u32(imageSize);
	u32(2835);
	u32(2835);
	u32(0);
	u32(0);

	std::vector<char> row(static_cast<std::size_t>(rowBytes + padding), 0);
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			const std::size_t src = (static_cast<std::size_t>(y) * w + x) * 4;
			row[x * 3 + 0] = static_cast<char>(rgba[src + 2]); // B
			row[x * 3 + 1] = static_cast<char>(rgba[src + 1]); // G
			row[x * 3 + 2] = static_cast<char>(rgba[src + 0]); // R
		}
		out.write(row.data(), static_cast<std::streamsize>(row.size()));
	}
	return static_cast<bool>(out);
}

// --- Shape preview (M3) -----------------------------------------------------

using assetexplorer::OrbitCamera;

// A loaded CN3Shape plus its orbit camera and display stats.
struct ShapePreview
{
	bool loaded = false;
	std::string error;

	std::unique_ptr<CN3Shape> shape;
	OrbitCamera camera;

	int partCount = 0;
	int triCount  = 0;
	int texCount  = 0;
	bool wireframe = false;
	bool demoCube  = false; // --demo: a synthetic cube to exercise the 3D path
};

// Draws a unit cube (per-face colors) through whatever view/projection is
// currently set - used by --demo and the render smoke to validate the camera /
// transform / depth path without a real .n3shape asset.
void DrawDemoCube(IRHIDevice* rhi)
{
	struct Vtx
	{
		float x, y, z;
		uint32_t color; // 0xAARRGGBB
	};
	auto face = [](Vtx* v, const float c[3][4], uint32_t col) {
		for (int i = 0; i < 6; ++i)
			v[i].color = col;
		v[0] = {c[0][0], c[0][1], c[0][2], col};
		v[1] = {c[1][0], c[1][1], c[1][2], col};
		v[2] = {c[2][0], c[2][1], c[2][2], col};
		v[3] = {c[0][0], c[0][1], c[0][2], col};
		v[4] = {c[2][0], c[2][1], c[2][2], col};
		v[5] = {c[3][0], c[3][1], c[3][2], col};
	};
	const float n = -1.0f, p = 1.0f;
	Vtx verts[36];
	const float px[4][4] = {{p, n, n, 0}, {p, p, n, 0}, {p, p, p, 0}, {p, n, p, 0}};
	const float nx[4][4] = {{n, n, p, 0}, {n, p, p, 0}, {n, p, n, 0}, {n, n, n, 0}};
	const float py[4][4] = {{n, p, n, 0}, {n, p, p, 0}, {p, p, p, 0}, {p, p, n, 0}};
	const float ny[4][4] = {{n, n, p, 0}, {n, n, n, 0}, {p, n, n, 0}, {p, n, p, 0}};
	const float pz[4][4] = {{p, n, p, 0}, {p, p, p, 0}, {n, p, p, 0}, {n, n, p, 0}};
	const float nz[4][4] = {{n, n, n, 0}, {n, p, n, 0}, {p, p, n, 0}, {p, n, n, 0}};
	face(&verts[0], px, 0xFFE24A4A);
	face(&verts[6], nx, 0xFF7A2A2A);
	face(&verts[12], py, 0xFF4AE26A);
	face(&verts[18], ny, 0xFF2A7A3A);
	face(&verts[24], pz, 0xFF4A78E2);
	face(&verts[30], nz, 0xFF2A3A7A);

	rhi->SetTexture(0, nullptr);
	rhi->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	rhi->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	rhi->SetFVF(FVF_XYZCOLOR);
	rhi->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 12, verts, sizeof(Vtx));
}

void ReleaseShapePreview(ShapePreview& sp)
{
	sp.shape.reset();
	sp = ShapePreview{};
}

void LoadShapePreview(ShapePreview& sp, const std::string& relPath, const std::string& name)
{
	ReleaseShapePreview(sp);

	auto shape = std::make_unique<CN3Shape>();
	if (!shape->LoadFromFile(relPath) || shape->PartCount() == 0)
	{
		sp.error = "Failed to load " + name;
		return;
	}
	shape->Tick(0.0f);      // resolve part matrices / texture animation index
	shape->FindMinMax();    // populate the bounding box for framing

	int tris = 0;
	int texs = 0;
	for (int i = 0; i < shape->PartCount(); ++i)
	{
		CN3SPart* part = shape->Part(i);
		if (part == nullptr)
			continue;
		if (const CN3PMesh* mesh = part->Mesh())
			tris += mesh->GetMaxNumIndices() / 3;
		texs += part->TexCount();
	}

	sp.partCount = shape->PartCount();
	sp.triCount  = tris;
	sp.texCount  = texs;
	sp.camera.FrameBounds(shape->Min(), shape->Max());
	sp.shape  = std::move(shape);
	sp.loaded = true;
}

using assetexplorer::AnimationPlayer;

// --- Character preview (M4) -------------------------------------------------

// A loaded CN3Chr with its skeleton-animation timeline, LOD, and orbit camera.
struct CharacterPreview
{
	bool loaded = false;
	std::string error;

	std::unique_ptr<CN3Chr> chr;
	OrbitCamera camera;
	AnimationPlayer player;

	std::vector<std::string> animNames;
	int animIndex = -1;
	int lod = 0;
	bool wireframe = false;
	bool showSkeleton = false;

	int jointCount = 0;
	int partCount  = 0;
	int plugCount  = 0;
};

void ReleaseCharacterPreview(CharacterPreview& cp)
{
	cp.chr.reset();
	cp = CharacterPreview{};
}

// Selects animation `iAni` and arms the timeline over its frame range.
void SelectAnimation(CharacterPreview& cp, int iAni)
{
	if (cp.chr == nullptr)
		return;
	CN3AnimControl* ctrl = cp.chr->AniCtrl();
	if (ctrl == nullptr || iAni < 0 || iAni >= ctrl->Count())
		return;

	cp.animIndex = iAni;
	cp.chr->AniCurSet(iAni); // establishes the current clip on the character
	if (const __AnimData* data = ctrl->DataGet(iAni))
	{
		const float fps = data->fFrmPerSec > 0.0f ? data->fFrmPerSec : 30.0f;
		cp.player.SetClip(data->fFrmStart, data->fFrmEnd, fps);
		cp.player.Play();
	}
}

void LoadCharacterPreview(CharacterPreview& cp, const std::string& relPath, const std::string& name)
{
	ReleaseCharacterPreview(cp);

	// The engine culls characters against the global camera when calculating
	// LOD; keep LODDelta at 0 so Render honours the LOD we set explicitly.
	CN3Chr::LODDeltaSet(0);

	auto chr = std::make_unique<CN3Chr>();
	if (!chr->LoadFromFile(relPath) || chr->PartCount() == 0)
	{
		cp.error = "Failed to load " + name;
		return;
	}
	chr->Tick(0.0f);
	chr->FindMinMax();

	// Count joints via the public matrix accessor (no size getter is exposed).
	int joints = 0;
	while (chr->MatrixGet(joints) != nullptr)
		++joints;

	cp.jointCount = joints;
	cp.partCount  = chr->PartCount();
	cp.plugCount  = chr->PlugCount();

	if (CN3AnimControl* ctrl = chr->AniCtrl())
	{
		for (int i = 0; i < ctrl->Count(); ++i)
		{
			const __AnimData* d = ctrl->DataGet(i);
			cp.animNames.push_back(d != nullptr && !d->szName.empty()
				? d->szName
				: ("anim " + std::to_string(i)));
		}
	}

	cp.camera.FrameBounds(chr->Min(), chr->Max());
	cp.chr = std::move(chr);
	if (!cp.animNames.empty())
		SelectAnimation(cp, 0);
	cp.loaded = true;
}

// --- FX bundle preview (M5) -------------------------------------------------

// A loaded CN3FXBundle played on a loop in an orbit viewport.
struct FXPreview
{
	bool loaded = false;
	std::string error;

	std::unique_ptr<CN3FXBundle> fx;
	OrbitCamera camera;
	bool playing = true;
	bool looping = true;

	std::string name;
	int moveType = 0;
	float life0  = 0.0f;
	int partCount = 0;
	std::array<int, 4> partTypeCounts{}; // particle/board/mesh/bottomboard
};

void ReleaseFXPreview(FXPreview& fp)
{
	if (fp.fx != nullptr)
		fp.fx->Stop(true);
	fp.fx.reset();
	fp = FXPreview{};
}

void LoadFXPreview(FXPreview& fp, const std::string& relPath, const std::string& name)
{
	ReleaseFXPreview(fp);

	auto fx = std::make_unique<CN3FXBundle>();
	if (!fx->LoadFromFile(relPath))
	{
		fp.error = "Failed to load " + name;
		return;
	}

	fx->m_vPos.Set(0.0f, 0.0f, 0.0f);
	fp.name     = fx->m_strName.empty() ? name : fx->m_strName;
	fp.moveType = fx->m_iMoveType;
	fp.life0    = fx->m_fLife0;

	int parts = 0;
	for (int i = 0; i < MAX_FX_PART; ++i)
	{
		CN3FXPartBase* part = fx->GetPart(i);
		if (part == nullptr)
			continue;
		++parts;
		const int t = part->m_iType;
		if (t >= FX_PART_TYPE_PARTICLE && t <= FX_PART_TYPE_BOTTOMBOARD)
			++fp.partTypeCounts[static_cast<std::size_t>(t - 1)];
	}
	fp.partCount = parts;

	// Effects have no authored bounding box; frame a fixed volume around the
	// origin that suits most skill/ambient effects. The user can zoom from there.
	fp.camera.FrameBounds(__Vector3(-4.0f, -1.0f, -4.0f), __Vector3(4.0f, 7.0f, 4.0f));
	fx->Trigger();
	fp.fx      = std::move(fx);
	fp.loaded  = true;
}

// --- Terrain preview (M6) ---------------------------------------------------

using assetexplorer::FlyCamera;

// A loaded CN3Terrain roamed with a fly camera. Placed objects / sky / water are
// deferred; this previews the terrain heightfield and its textures.
struct TerrainPreview
{
	bool loaded = false;
	std::string error;

	std::unique_ptr<CN3Terrain> terrain;
	FlyCamera camera;
	float widthMeters = 0.0f;
	float moveSpeed   = 60.0f; // meters/second, scaled to the map on load
	bool wireframe = false;
};

void ReleaseTerrainPreview(TerrainPreview& tp)
{
	tp.terrain.reset();
	tp = TerrainPreview{};
}

void LoadTerrainPreview(TerrainPreview& tp, const std::string& relPath, const std::string& name)
{
	ReleaseTerrainPreview(tp);

	auto terrain = std::make_unique<CN3Terrain>();
	if (!terrain->LoadFromFile(relPath) || terrain->GetWidthByMeter() <= 0.0f)
	{
		tp.error = "Failed to load " + name;
		return;
	}

	const float w = terrain->GetWidthByMeter();
	tp.widthMeters = w;
	tp.moveSpeed   = std::max(w * 0.06f, 20.0f);

	// Start above the map centre, looking down and forward into it.
	const float cx = w * 0.5f;
	const float cz = w * 0.5f;
	const float ground = terrain->GetHeight(cx, cz);
	tp.camera.SetPosition(__Vector3(cx, ground + w * 0.12f, cz - w * 0.12f));
	tp.camera.SetLook(0.0f, -0.6f);

	tp.terrain = std::move(terrain);
	tp.loaded  = true;
}

// All UI/session state for the explorer.
struct ExplorerState
{
	AssetIndex index;
	fs::path dataDir;
	std::string status;

	char search[256] = {};
	unsigned categoryMask = 0; // 0 == all
	int selected = -1;         // index into index.Entries(), or -1
	int loadedForSelection = -2; // which selection the preview reflects

	// Preview render target (M1). Sized to the viewport panel; recreated when it
	// changes. viewportSize is measured during the UI pass and consumed at the
	// top of the next frame so the target stays stable within a frame.
	IRHIDevice* rhi = nullptr;
	IRHIRenderTarget* previewRT = nullptr;
	int rtWidth  = 0;
	int rtHeight = 0;
	int viewportW = 0;
	int viewportH = 0;

	TexturePreview tex;        // populated when a texture asset is selected (M2)
	ShapePreview shape;        // populated when a shape asset is selected (M3)
	CharacterPreview character; // populated when a character is selected (M4)
	FXPreview fx;              // populated when an FX bundle is selected (M5)
	TerrainPreview terrain;   // populated when a terrain/map is selected (M6)
	CN3Camera engineCamera;    // drives s_CameraData for character/FX/terrain

	fs::path AbsPathOf(const AssetEntry& e) const
	{
		return dataDir / fs::path(e.relativePath);
	}
};

// Renders the loaded character into the viewport-sized target (M4). Unlike the
// shape path this drives the engine CN3Camera so s_CameraData is populated
// (CN3Chr culls/LODs against it), poses the skeleton to the timeline's current
// frame, and forces the chosen LOD.
void RenderCharacterToRT(ExplorerState& state)
{
	CharacterPreview& cp = state.character;
	if (state.rhi == nullptr || !cp.loaded || cp.chr == nullptr)
		return;
	if (state.viewportW <= 0 || state.viewportH <= 0)
		return;

	if (state.previewRT == nullptr || state.rtWidth != state.viewportW
		|| state.rtHeight != state.viewportH)
	{
		delete state.previewRT;
		RHIRenderTargetDesc desc;
		desc.width  = static_cast<UINT>(state.viewportW);
		desc.height = static_cast<UINT>(state.viewportH);
		desc.depth  = true;
		state.previewRT = state.rhi->CreateRenderTarget(desc);
		state.rtWidth   = state.viewportW;
		state.rtHeight  = state.viewportH;
	}
	if (state.previewRT == nullptr)
		return;

	state.rhi->BeginRenderTarget(state.previewRT);
	state.rhi->Clear(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF14161B, 1.0f, 0);

	// Drive the engine camera from the orbit camera; Apply() sets the view/proj
	// transforms and updates s_CameraData (used by CN3Chr's LOD/frustum checks).
	const __Vector3 eye = cp.camera.Eye();
	const __Vector3 at  = cp.camera.Target();
	CN3Camera& cam = state.engineCamera;
	cam.m_bFogUse   = FALSE;
	cam.m_Data.fFOV = 0.9f;
	cam.m_Data.fNP  = cp.camera.NearPlane();
	cam.m_Data.fFP  = std::max(cp.camera.FarPlane(), 512.0f);
	cam.LookAt(eye, at, __Vector3(0.0f, 1.0f, 0.0f));
	cam.Tick();
	cam.Apply();

	state.rhi->SetRenderState(D3DRS_LIGHTING, FALSE);
	state.rhi->SetRenderState(D3DRS_ZENABLE, TRUE);
	state.rhi->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	state.rhi->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	state.rhi->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	state.rhi->SetRenderState(D3DRS_FILLMODE,
		cp.wireframe ? D3DFILL_WIREFRAME : D3DFILL_SOLID);

	cp.chr->Tick(cp.player.Frame()); // pose the skeleton to the current frame
	cp.chr->m_nLOD = std::clamp(cp.lod, 0, MAX_CHR_LOD - 1); // force the chosen LOD
	cp.chr->Render();

	state.rhi->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	state.rhi->EndRenderTarget();
}

// Renders (and ticks, when playing) the FX bundle into the target (M5). FX parts
// are camera-facing billboards/particles, so - like characters - the engine
// camera is driven to populate s_CameraData. Timing comes from s_fSecPerFrm,
// which the tool sets from the frame delta. Mirrors CN3FXMgr's render state.
void RenderFXToRT(ExplorerState& state, float dtSeconds)
{
	FXPreview& fp = state.fx;
	if (state.rhi == nullptr || !fp.loaded || fp.fx == nullptr)
		return;
	if (state.viewportW <= 0 || state.viewportH <= 0)
		return;

	if (state.previewRT == nullptr || state.rtWidth != state.viewportW
		|| state.rtHeight != state.viewportH)
	{
		delete state.previewRT;
		RHIRenderTargetDesc desc;
		desc.width  = static_cast<UINT>(state.viewportW);
		desc.height = static_cast<UINT>(state.viewportH);
		desc.depth  = true;
		state.previewRT = state.rhi->CreateRenderTarget(desc);
		state.rtWidth   = state.viewportW;
		state.rtHeight  = state.viewportH;
	}
	if (state.previewRT == nullptr)
		return;

	// FX advance uses the engine's per-frame delta.
	CN3Base::s_fSecPerFrm = std::clamp(dtSeconds, 0.001f, 0.1f);
	CN3Base::s_fFrmPerSec = 1.0f / CN3Base::s_fSecPerFrm;

	state.rhi->BeginRenderTarget(state.previewRT);
	state.rhi->Clear(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF14161B, 1.0f, 0);

	const __Vector3 eye = fp.camera.Eye();
	const __Vector3 at  = fp.camera.Target();
	CN3Camera& cam = state.engineCamera;
	cam.m_bFogUse   = FALSE;
	cam.m_Data.fFOV = 0.9f;
	cam.m_Data.fNP  = fp.camera.NearPlane();
	cam.m_Data.fFP  = std::max(fp.camera.FarPlane(), 512.0f);
	cam.LookAt(eye, at, __Vector3(0.0f, 1.0f, 0.0f));
	cam.Tick();
	cam.Apply();

	if (fp.playing)
	{
		fp.fx->Tick();
		if (fp.fx->GetState() == FX_BUNDLE_STATE_DEAD && fp.looping)
			fp.fx->Trigger();
	}

	// Same state CN3FXMgr uses: unlit, additive/alpha parts (parts set their own
	// blend), no depth writes so overlapping particles don't self-occlude.
	state.rhi->SetRenderState(D3DRS_LIGHTING, FALSE);
	state.rhi->SetRenderState(D3DRS_ZENABLE, TRUE);
	state.rhi->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	state.rhi->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	state.rhi->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	fp.fx->Render();

	state.rhi->EndRenderTarget();
}

// Renders the loaded terrain into the target with the fly camera (M6). The
// engine streams patches around the camera's XZ position, so the camera drives
// s_CameraData (via CN3Camera) and the terrain is ticked before rendering.
void RenderTerrainToRT(ExplorerState& state)
{
	TerrainPreview& tp = state.terrain;
	if (state.rhi == nullptr || !tp.loaded || tp.terrain == nullptr)
		return;
	if (state.viewportW <= 0 || state.viewportH <= 0)
		return;

	if (state.previewRT == nullptr || state.rtWidth != state.viewportW
		|| state.rtHeight != state.viewportH)
	{
		delete state.previewRT;
		RHIRenderTargetDesc desc;
		desc.width  = static_cast<UINT>(state.viewportW);
		desc.height = static_cast<UINT>(state.viewportH);
		desc.depth  = true;
		state.previewRT = state.rhi->CreateRenderTarget(desc);
		state.rtWidth   = state.viewportW;
		state.rtHeight  = state.viewportH;
	}
	if (state.previewRT == nullptr)
		return;

	const float aspect = static_cast<float>(state.rtWidth) / static_cast<float>(state.rtHeight);
	const __Vector3 eye = tp.camera.Position();
	const __Vector3 at  = eye + tp.camera.Forward();

	state.rhi->BeginRenderTarget(state.previewRT);
	state.rhi->Clear(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF1A2230, 1.0f, 0);

	CN3Camera& cam = state.engineCamera;
	cam.m_bFogUse   = FALSE;
	cam.m_Data.fFOV = 0.9f;
	cam.m_Data.fNP  = 1.0f;
	// The far plane also drives the terrain LOD (iLOD = 3*fFP/512), so keep it
	// generous enough to load a decent patch window around the camera.
	cam.m_Data.fFP  = std::clamp(tp.widthMeters * 0.5f, 512.0f, 4096.0f);
	cam.LookAt(eye, at, __Vector3(0.0f, 1.0f, 0.0f));
	cam.Tick();
	cam.Apply();
	// CN3Camera::Tick reads aspect from the viewport, which BeginRenderTarget set;
	// override defensively in case the projection needs the exact RT aspect.
	cam.m_Data.mtxProjection.PerspectiveFovLH(cam.m_Data.fFOV, aspect, cam.m_Data.fNP,
		cam.m_Data.fFP);
	state.rhi->SetTransform(D3DTS_PROJECTION, cam.m_Data.mtxProjection.toD3D());

	state.rhi->SetRenderState(D3DRS_LIGHTING, FALSE);
	state.rhi->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	tp.terrain->SetFillMode(tp.wireframe ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
	tp.terrain->Tick();   // stream patches around the camera
	tp.terrain->Render(); // sets its own multi-texture / cull / z state

	state.rhi->EndRenderTarget();
}

// Renders the loaded shape into the viewport-sized render target with the orbit
// camera (M3). The RT is (re)created to match the panel; the engine's own part
// render sets world matrices, textures and materials, so we only supply the
// view/projection and a neutral unlit-textured state.
void RenderShapeToRT(ExplorerState& state)
{
	if (state.rhi == nullptr || !state.shape.loaded)
		return;
	if (state.shape.shape == nullptr && !state.shape.demoCube)
		return;
	if (state.viewportW <= 0 || state.viewportH <= 0)
		return;

	if (state.previewRT == nullptr || state.rtWidth != state.viewportW
		|| state.rtHeight != state.viewportH)
	{
		delete state.previewRT;
		RHIRenderTargetDesc desc;
		desc.width  = static_cast<UINT>(state.viewportW);
		desc.height = static_cast<UINT>(state.viewportH);
		desc.depth  = true;
		state.previewRT = state.rhi->CreateRenderTarget(desc);
		state.rtWidth   = state.viewportW;
		state.rtHeight  = state.viewportH;
	}
	if (state.previewRT == nullptr)
		return;

	const float aspect = static_cast<float>(state.rtWidth) / static_cast<float>(state.rtHeight);
	const __Matrix44 view = state.shape.camera.View();
	const __Matrix44 proj = state.shape.camera.Projection(aspect);

	state.rhi->BeginRenderTarget(state.previewRT);
	state.rhi->Clear(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF14161B, 1.0f, 0);
	state.rhi->SetTransform(D3DTS_VIEW, view.toD3D());
	state.rhi->SetTransform(D3DTS_PROJECTION, proj.toD3D());
	state.rhi->SetRenderState(D3DRS_LIGHTING, FALSE);
	state.rhi->SetRenderState(D3DRS_ZENABLE, TRUE);
	state.rhi->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	state.rhi->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	// Show both sides so back-facing or reverse-wound parts are never invisible.
	state.rhi->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	state.rhi->SetRenderState(D3DRS_FILLMODE,
		state.shape.wireframe ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
	if (state.shape.demoCube)
		DrawDemoCube(state.rhi);
	else
		state.shape.shape->Render();
	state.rhi->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
	state.rhi->EndRenderTarget();
}

// Exports an engine texture (.dxt/.tga) by rendering it into an offscreen RGBA
// target and reading it back to a 24-bit BMP - self-contained, no encoder
// dependency, and the same readback path thumbnails will use. Returns false on
// any failure.
bool ExportEngineTextureToBmp(ExplorerState& state, const fs::path& dst)
{
	TexturePreview& tp = state.tex;
	auto* rhi = dynamic_cast<RHIDeviceGL*>(state.rhi);
	if (rhi == nullptr || tp.n3tex == nullptr || tp.width <= 0 || tp.height <= 0)
		return false;

	RHIRenderTargetDesc desc;
	desc.width  = static_cast<UINT>(tp.width);
	desc.height = static_cast<UINT>(tp.height);
	desc.depth  = false;
	IRHIRenderTarget* rt = rhi->CreateRenderTarget(desc);
	if (rt == nullptr)
		return false;

	struct VtxRHWT
	{
		float x, y, z, rhw;
		float u, v;
	};
	const float w = static_cast<float>(tp.width);
	const float h = static_cast<float>(tp.height);
	// Two triangles covering the whole target, sampling the full texture.
	const VtxRHWT quad[6] = {
		{0, 0, 0, 1, 0, 0}, {w, 0, 0, 1, 1, 0}, {w, h, 0, 1, 1, 1},
		{0, 0, 0, 1, 0, 0}, {w, h, 0, 1, 1, 1}, {0, h, 0, 1, 0, 1},
	};

	rhi->BeginRenderTarget(rt);
	rhi->Clear(D3DCLEAR_TARGET, 0x00000000, 1.0f, 0);
	rhi->SetRenderState(D3DRS_LIGHTING, FALSE);
	rhi->SetRenderState(D3DRS_ZENABLE, FALSE);
	rhi->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	rhi->SetTexture(0, tp.n3tex->Get());
	rhi->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	rhi->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	rhi->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
	rhi->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(VtxRHWT));
	rhi->EndRenderTarget();

	std::vector<uint8_t> rgba(static_cast<std::size_t>(tp.width) * tp.height * 4);
	const bool read = rhi->ReadRenderTargetRGBA(rt, rgba.data());
	delete rt;
	if (!read)
		return false;

	return WriteBmp24(dst, rgba, tp.width, tp.height);
}


void RescanDataDir(ExplorerState& state, const fs::path& dir)
{
	state.dataDir = dir;
	state.selected = -1;
	// Point the engine's asset root at the data dir (as the WarFare client does),
	// so assets referenced by relative path inside shapes/characters/FX bundles -
	// most importantly their textures - resolve. Engine loads then use paths
	// relative to this root; only filesystem-level reads (.ksc, exports) use the
	// absolute path.
	CN3Base::PathSet(dir.string());
	const std::size_t n = state.index.Scan(dir);
	state.status = n == 0
		? "No assets found under " + dir.string()
		: std::to_string(n) + " assets indexed under " + dir.string();
}

void DrawToolbar(ExplorerState& state)
{
	if (ImGui::Button("Rescan"))
		RescanDataDir(state, state.dataDir);
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();

	ImGui::SetNextItemWidth(280);
	ImGui::InputTextWithHint("##search", "Search assets...", state.search, sizeof(state.search));
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();

	// Category filter chips: click to toggle the category's bit in the mask.
	const auto counts = state.index.CountsByCategory();
	for (AssetCategory cat : kFilterChips)
	{
		const unsigned bit = CategoryBit(cat);
		const bool on = (state.categoryMask & bit) != 0;
		const std::size_t count = counts[static_cast<std::size_t>(cat)];

		char label[64];
		std::snprintf(label, sizeof(label), "%s (%zu)", AssetCategoryLabel(cat), count);

		if (on)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		if (ImGui::Button(label))
			state.categoryMask ^= bit;
		if (on)
			ImGui::PopStyleColor();
		ImGui::SameLine();
	}
	if (state.categoryMask != 0)
	{
		if (ImGui::Button("Clear"))
			state.categoryMask = 0;
	}
	else
	{
		ImGui::TextDisabled("all");
	}
}

void DrawAssetList(ExplorerState& state, const ImVec2& size)
{
	if (!ImGui::BeginChild("##assetlist", size, ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}

	const std::vector<std::size_t> shown = state.index.Filter(state.search, state.categoryMask);
	ImGui::Text("%zu / %zu assets", shown.size(), state.index.Entries().size());
	ImGui::Separator();

	// A clipper keeps a multi-thousand-entry list smooth.
	ImGuiListClipper clipper;
	clipper.Begin(static_cast<int>(shown.size()));
	while (clipper.Step())
	{
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
		{
			const std::size_t entryIdx = shown[static_cast<std::size_t>(row)];
			const AssetEntry& e = state.index.Entries()[entryIdx];

			char label[512];
			std::snprintf(label, sizeof(label), "[%s] %s",
				AssetCategoryLabel(e.category), e.relativePath.c_str());

			const bool selected = (state.selected == static_cast<int>(entryIdx));
			if (ImGui::Selectable(label, selected))
				state.selected = static_cast<int>(entryIdx);
		}
	}
	ImGui::EndChild();
}

// Draws the loaded texture with a checkerboard/solid backdrop and fit/zoom/pan.
void DrawTextureCanvas(TexturePreview& tp)
{
	// Toolbar.
	ImGui::SetNextItemWidth(120);
	const char* bgs[] = {"Checker", "Black", "White"};
	ImGui::Combo("##bg", &tp.background, bgs, 3);
	ImGui::SameLine();
	if (ImGui::Button("Fit"))
	{
		tp.zoom = 0.0f;
		tp.pan  = ImVec2(0, 0);
	}
	ImGui::SameLine();
	if (ImGui::Button("1:1"))
	{
		tp.zoom = 1.0f;
		tp.pan  = ImVec2(0, 0);
	}

	const ImVec2 canvas = ImGui::GetContentRegionAvail();
	const ImVec2 p0     = ImGui::GetCursorScreenPos();
	const ImVec2 p1(p0.x + canvas.x, p0.y + canvas.y);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->PushClipRect(p0, p1, true);

	// Backdrop.
	if (tp.background == 1)
		dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 255));
	else if (tp.background == 2)
		dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 255));
	else
	{
		const float cell = 12.0f;
		dl->AddRectFilled(p0, p1, IM_COL32(60, 62, 68, 255));
		for (float y = p0.y; y < p1.y; y += cell)
			for (float x = p0.x; x < p1.x; x += cell)
			{
				const int cxi = static_cast<int>((x - p0.x) / cell);
				const int cyi = static_cast<int>((y - p0.y) / cell);
				if (((cxi + cyi) & 1) == 0)
					dl->AddRectFilled(ImVec2(x, y),
						ImVec2(x + cell < p1.x ? x + cell : p1.x, y + cell < p1.y ? y + cell : p1.y),
						IM_COL32(44, 46, 52, 255));
			}
	}

	// Fit-to-canvas scale unless the user pinned a pixel zoom.
	const float fitScale = std::min(canvas.x / tp.width, canvas.y / tp.height);
	const float scale    = (tp.zoom <= 0.0f) ? fitScale : tp.zoom;
	const ImVec2 imgSize(tp.width * scale, tp.height * scale);
	const ImVec2 center(p0.x + canvas.x * 0.5f + tp.pan.x, p0.y + canvas.y * 0.5f + tp.pan.y);
	const ImVec2 imin(center.x - imgSize.x * 0.5f, center.y - imgSize.y * 0.5f);
	const ImVec2 imax(imin.x + imgSize.x, imin.y + imgSize.y);
	// Both texture paths upload top-down, so ImGui's top-left UVs display upright.
	dl->AddImage(static_cast<ImTextureID>(tp.glTexture), imin, imax);
	dl->PopClipRect();

	// Interaction: wheel zooms about center, drag pans.
	ImGui::SetCursorScreenPos(p0);
	ImGui::InvisibleButton("##texcanvas", canvas);
	if (ImGui::IsItemHovered())
	{
		const float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0.0f)
		{
			const float base = (tp.zoom <= 0.0f) ? fitScale : tp.zoom;
			tp.zoom          = std::clamp(base * (1.0f + wheel * 0.1f), 0.02f, 64.0f);
		}
	}
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		const ImVec2 d = ImGui::GetIO().MouseDelta;
		tp.pan.x += d.x;
		tp.pan.y += d.y;
	}
}

// Shows an RT image filling `canvas` and maps drag->orbit, wheel->dolly onto
// the given camera. Shared by the shape (M3) and character (M4) viewports.
void DrawOrbitImage(IRHIRenderTarget* rt, OrbitCamera& cam, const char* id, const ImVec2& canvas)
{
	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	// GL renders bottom-up into the target; flip V so it displays upright.
	ImGui::Image(reinterpret_cast<ImTextureID>(rt->ColorHandle()), canvas, ImVec2(0, 1), ImVec2(1, 0));

	ImGui::SetCursorScreenPos(cursor);
	ImGui::InvisibleButton(id, canvas);
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		const ImVec2 d = ImGui::GetIO().MouseDelta;
		cam.Orbit(d.x * 0.01f, d.y * 0.01f);
	}
	if (ImGui::IsItemHovered())
	{
		const float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0.0f)
			cam.Dolly(1.0f - wheel * 0.1f);
	}
}

// Character viewport: animation/LOD toolbar, the orbit image, and a scrub
// timeline (M4).
void DrawCharacterViewport(ExplorerState& state)
{
	CharacterPreview& cp = state.character;

	// --- Toolbar row 1: animation selection + transport ---
	if (!cp.animNames.empty())
	{
		ImGui::SetNextItemWidth(200);
		const char* cur = (cp.animIndex >= 0 && cp.animIndex < static_cast<int>(cp.animNames.size()))
			? cp.animNames[static_cast<std::size_t>(cp.animIndex)].c_str()
			: "(none)";
		if (ImGui::BeginCombo("##anim", cur))
		{
			for (int i = 0; i < static_cast<int>(cp.animNames.size()); ++i)
			{
				const bool sel = (i == cp.animIndex);
				if (ImGui::Selectable(cp.animNames[static_cast<std::size_t>(i)].c_str(), sel))
					SelectAnimation(cp, i);
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::Button(cp.player.IsPlaying() ? "Pause" : "Play"))
			cp.player.TogglePlay();
		ImGui::SameLine();
		bool loop = cp.player.IsLooping();
		if (ImGui::Checkbox("Loop", &loop))
			cp.player.SetLooping(loop);
		ImGui::SameLine();
		float speed = cp.player.Speed();
		ImGui::SetNextItemWidth(120);
		if (ImGui::SliderFloat("Speed", &speed, 0.1f, 4.0f, "%.2fx"))
			cp.player.SetSpeed(speed);
	}
	else
	{
		ImGui::TextDisabled("no animations");
	}

	// --- Toolbar row 2: LOD + display toggles ---
	ImGui::SetNextItemWidth(90);
	const char* lods[] = {"LOD 0", "LOD 1", "LOD 2", "LOD 3"};
	ImGui::Combo("##lod", &cp.lod, lods, MAX_CHR_LOD);
	ImGui::SameLine();
	ImGui::Checkbox("Wireframe", &cp.wireframe);
	ImGui::SameLine();
	if (ImGui::Button("Reset view") && cp.chr != nullptr)
		cp.camera.FrameBounds(cp.chr->Min(), cp.chr->Max());
	ImGui::SameLine();
	ImGui::TextDisabled("drag: orbit  |  wheel: zoom");

	// --- Orbit image (leave room for the timeline) ---
	const float timelineH = ImGui::GetFrameHeightWithSpacing();
	ImVec2 canvas = ImGui::GetContentRegionAvail();
	canvas.y = canvas.y > timelineH ? canvas.y - timelineH : canvas.y;
	if (state.previewRT != nullptr && state.previewRT->ColorHandle() != nullptr)
		DrawOrbitImage(state.previewRT, cp.camera, "##chrcanvas", canvas);

	// --- Timeline scrub ---
	float frame = cp.player.Frame();
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::SliderFloat("##timeline", &frame, cp.player.StartFrame(), cp.player.EndFrame(),
			"frame %.0f"))
		cp.player.Scrub(frame);
}

// FX viewport: play/restart/loop transport over the orbit image (M5).
void DrawFXViewport(ExplorerState& state)
{
	FXPreview& fp = state.fx;

	if (ImGui::Button(fp.playing ? "Pause" : "Play"))
		fp.playing = !fp.playing;
	ImGui::SameLine();
	if (ImGui::Button("Restart") && fp.fx != nullptr)
	{
		fp.fx->Stop(true);
		fp.fx->Trigger();
		fp.playing = true;
	}
	ImGui::SameLine();
	ImGui::Checkbox("Loop", &fp.looping);
	ImGui::SameLine();
	if (ImGui::Button("Reset view"))
		fp.camera.FrameBounds(__Vector3(-4.0f, -1.0f, -4.0f), __Vector3(4.0f, 7.0f, 4.0f));
	ImGui::SameLine();
	ImGui::TextDisabled("drag: orbit  |  wheel: zoom");

	if (state.previewRT != nullptr && state.previewRT->ColorHandle() != nullptr)
		DrawOrbitImage(state.previewRT, fp.camera, "##fxcanvas", ImGui::GetContentRegionAvail());
}

// Terrain viewport: fly-camera roam (WASD/QE + drag look) over the map (M6).
void DrawTerrainViewport(ExplorerState& state)
{
	TerrainPreview& tp = state.terrain;

	ImGui::Checkbox("Wireframe", &tp.wireframe);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(160);
	ImGui::SliderFloat("Speed", &tp.moveSpeed, 5.0f, 400.0f, "%.0f m/s");
	ImGui::SameLine();
	ImGui::TextDisabled("WASD/QE: move  |  drag: look");

	const ImVec2 canvas = ImGui::GetContentRegionAvail();
	if (state.previewRT == nullptr || state.previewRT->ColorHandle() == nullptr)
		return;

	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	ImGui::Image(reinterpret_cast<ImTextureID>(state.previewRT->ColorHandle()), canvas,
		ImVec2(0, 1), ImVec2(1, 0));

	ImGui::SetCursorScreenPos(cursor);
	ImGui::InvisibleButton("##terraincanvas", canvas);
	const bool active = ImGui::IsItemActive();
	if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		const ImVec2 d = ImGui::GetIO().MouseDelta;
		tp.camera.Look(d.x * 0.005f, -d.y * 0.005f);
	}
	if (ImGui::IsItemHovered() || active)
	{
		const float d = tp.moveSpeed * ImGui::GetIO().DeltaTime;
		if (ImGui::IsKeyDown(ImGuiKey_W)) tp.camera.MoveForward(d);
		if (ImGui::IsKeyDown(ImGuiKey_S)) tp.camera.MoveForward(-d);
		if (ImGui::IsKeyDown(ImGuiKey_D)) tp.camera.MoveRight(d);
		if (ImGui::IsKeyDown(ImGuiKey_A)) tp.camera.MoveRight(-d);
		if (ImGui::IsKeyDown(ImGuiKey_E)) tp.camera.MoveUp(d);
		if (ImGui::IsKeyDown(ImGuiKey_Q)) tp.camera.MoveUp(-d);
	}
}

void DrawViewport(ExplorerState& state, const ImVec2& size)
{
	if (!ImGui::BeginChild("##viewport", size, ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	state.viewportW = static_cast<int>(avail.x > 1.0f ? avail.x : 1.0f);
	state.viewportH = static_cast<int>(avail.y > 1.0f ? avail.y : 1.0f);

	const bool haveSel = state.selected >= 0
		&& state.selected < static_cast<int>(state.index.Entries().size());

	auto centeredText = [&](const char* msg) {
		const ImVec2 sz = ImGui::CalcTextSize(msg);
		ImGui::SetCursorPos(ImVec2((avail.x - sz.x) * 0.5f, avail.y * 0.5f));
		ImGui::TextDisabled("%s", msg);
	};

	if (state.tex.loaded)
	{
		DrawTextureCanvas(state.tex);
	}
	else if (state.character.loaded)
	{
		DrawCharacterViewport(state);
	}
	else if (state.fx.loaded)
	{
		DrawFXViewport(state);
	}
	else if (state.terrain.loaded)
	{
		DrawTerrainViewport(state);
	}
	else if (state.shape.loaded && state.previewRT != nullptr
		&& state.previewRT->ColorHandle() != nullptr)
	{
		// Toolbar.
		ImGui::Checkbox("Wireframe", &state.shape.wireframe);
		ImGui::SameLine();
		if (ImGui::Button("Reset view"))
		{
			if (state.shape.shape != nullptr)
				state.shape.camera.FrameBounds(state.shape.shape->Min(), state.shape.shape->Max());
			else
				state.shape.camera.FrameBounds(__Vector3(-1, -1, -1), __Vector3(1, 1, 1));
		}
		ImGui::SameLine();
		ImGui::TextDisabled("drag: orbit  |  wheel: zoom");
		DrawOrbitImage(state.previewRT, state.shape.camera, "##shapecanvas",
			ImGui::GetContentRegionAvail());
	}
	else if (!state.tex.error.empty())
	{
		centeredText(state.tex.error.c_str());
	}
	else if (!state.character.error.empty())
	{
		centeredText(state.character.error.c_str());
	}
	else if (!state.fx.error.empty())
	{
		centeredText(state.fx.error.c_str());
	}
	else if (!state.terrain.error.empty())
	{
		centeredText(state.terrain.error.c_str());
	}
	else if (!state.shape.error.empty())
	{
		centeredText(state.shape.error.c_str());
	}
	else if (haveSel)
	{
		const AssetEntry& e = state.index.Entries()[static_cast<std::size_t>(state.selected)];
		char msg[128];
		std::snprintf(msg, sizeof(msg), "Preview for %s arrives in a later milestone.",
			AssetTypeName(e.type));
		centeredText(msg);
	}
	else
	{
		centeredText("Select an asset to preview.");
	}
	ImGui::EndChild();
}

void DrawInspector(ExplorerState& state, const ImVec2& size)
{
	if (!ImGui::BeginChild("##inspector", size, ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}
	ImGui::TextUnformatted("Inspector");
	ImGui::Separator();

	if (state.selected < 0
		|| state.selected >= static_cast<int>(state.index.Entries().size()))
	{
		ImGui::TextDisabled("Select an asset to inspect.");
		ImGui::EndChild();
		return;
	}

	const AssetEntry& e = state.index.Entries()[static_cast<std::size_t>(state.selected)];
	ImGui::Text("Name");
	ImGui::TextWrapped("%s", e.fileName.c_str());
	ImGui::Spacing();
	ImGui::Text("Path");
	ImGui::TextWrapped("%s", e.relativePath.c_str());
	ImGui::Spacing();
	ImGui::Text("Type");
	ImGui::TextDisabled("%s", AssetTypeName(e.type));
	ImGui::Spacing();
	ImGui::Text("Size");
	ImGui::TextDisabled("%s", HumanSize(e.sizeBytes).c_str());

	// Texture-specific metadata + export.
	const TexturePreview& tp = state.tex;
	if (tp.loaded)
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::Text("Dimensions");
		ImGui::TextDisabled("%d x %d", tp.width, tp.height);
		ImGui::Spacing();
		ImGui::Text("Format");
		ImGui::TextDisabled("%s", tp.formatName.c_str());
		ImGui::Spacing();
		ImGui::Text("Mip levels");
		ImGui::TextDisabled("%d", tp.mipCount);
		ImGui::Spacing();
		ImGui::Text("Alpha");
		ImGui::TextDisabled("%s", tp.hasAlpha ? "yes" : "no");

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		const char* label = tp.isKsc ? "Export JPEG" : "Export BMP";
		if (ImGui::Button(label, ImVec2(-FLT_MIN, 0)))
		{
			const fs::path src = state.AbsPathOf(e);
			if (tp.isKsc)
			{
				fs::path dst = src;
				dst.replace_extension(".jpg");
				state.status = ksc_core::ExportJpg(src, dst)
					? "Exported " + dst.string()
					: "Export failed for " + dst.string();
			}
			else
			{
				fs::path dst = src;
				dst.replace_extension(".bmp");
				state.status = ExportEngineTextureToBmp(state, dst)
					? "Exported " + dst.string()
					: "Export failed for " + dst.string();
			}
		}
		ImGui::TextDisabled("writes next to the source file");
	}
	else if (!tp.error.empty())
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1.0f), "%s", tp.error.c_str());
	}

	// Shape-specific metadata.
	const ShapePreview& sp = state.shape;
	if (sp.loaded)
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::Text("Parts");
		ImGui::TextDisabled("%d", sp.partCount);
		ImGui::Spacing();
		ImGui::Text("Triangles");
		ImGui::TextDisabled("%d", sp.triCount);
		ImGui::Spacing();
		ImGui::Text("Textures");
		ImGui::TextDisabled("%d", sp.texCount);
	}
	else if (!sp.error.empty())
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1.0f), "%s", sp.error.c_str());
	}

	// Character-specific metadata.
	const CharacterPreview& cp = state.character;
	if (cp.loaded)
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::Text("Joints");
		ImGui::TextDisabled("%d", cp.jointCount);
		ImGui::Spacing();
		ImGui::Text("Parts");
		ImGui::TextDisabled("%d", cp.partCount);
		ImGui::Spacing();
		ImGui::Text("Plugs");
		ImGui::TextDisabled("%d", cp.plugCount);
		ImGui::Spacing();
		ImGui::Text("Animations");
		ImGui::TextDisabled("%d", static_cast<int>(cp.animNames.size()));
	}
	else if (!cp.error.empty())
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1.0f), "%s", cp.error.c_str());
	}

	// FX-specific metadata.
	const FXPreview& fp = state.fx;
	if (fp.loaded)
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::Text("Bundle");
		ImGui::TextDisabled("%s", fp.name.c_str());
		ImGui::Spacing();
		ImGui::Text("Move type");
		ImGui::TextDisabled("%d", fp.moveType);
		ImGui::Spacing();
		ImGui::Text("Lifetime");
		ImGui::TextDisabled("%.2fs", fp.life0);
		ImGui::Spacing();
		ImGui::Text("Parts");
		ImGui::TextDisabled("%d  (%dp %db %dm %dbb)", fp.partCount, fp.partTypeCounts[0],
			fp.partTypeCounts[1], fp.partTypeCounts[2], fp.partTypeCounts[3]);
		ImGui::Spacing();
		ImGui::Text("State");
		const char* st = fp.fx == nullptr ? "-"
			: (fp.fx->GetState() == FX_BUNDLE_STATE_LIVE ? "live"
				: (fp.fx->GetState() == FX_BUNDLE_STATE_DYING ? "dying" : "dead"));
		ImGui::TextDisabled("%s", st);
	}
	else if (!fp.error.empty())
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1.0f), "%s", fp.error.c_str());
	}

	// Terrain-specific metadata.
	const TerrainPreview& trp = state.terrain;
	if (trp.loaded)
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::Text("Map width");
		ImGui::TextDisabled("%.0f m", trp.widthMeters);
		ImGui::Spacing();
		ImGui::Text("LOD level");
		ImGui::TextDisabled("%d", trp.terrain != nullptr ? trp.terrain->GetLODLevel() : 0);
		ImGui::Spacing();
		ImGui::Text("Tiles");
		ImGui::TextDisabled("%s", (trp.terrain != nullptr && trp.terrain->m_bAvailableTile)
			? "available" : "none");
		ImGui::Spacing();
		ImGui::TextDisabled("Placed objects, sky and water are not shown yet.");
	}
	else if (!trp.error.empty())
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1.0f), "%s", trp.error.c_str());
	}
	ImGui::EndChild();
}

// Loads (or clears) the preview to match the current selection. Called once a
// frame; only re-loads when the selection actually changed.
void SyncPreview(ExplorerState& state)
{
	if (state.selected == state.loadedForSelection)
		return;
	state.loadedForSelection = state.selected;

	auto clearAll = [&]() {
		ReleaseTexturePreview(state.tex);
		ReleaseShapePreview(state.shape);
		ReleaseCharacterPreview(state.character);
		ReleaseFXPreview(state.fx);
		ReleaseTerrainPreview(state.terrain);
	};

	if (state.selected < 0 || state.selected >= static_cast<int>(state.index.Entries().size()))
	{
		clearAll();
		return;
	}

	const AssetEntry& e = state.index.Entries()[static_cast<std::size_t>(state.selected)];
	clearAll();
	if (e.category == AssetCategory::Texture)
		LoadTexturePreview(state.tex, e.type, e.relativePath, state.AbsPathOf(e));
	else if (e.type == AssetType::Shape)
		LoadShapePreview(state.shape, e.relativePath, e.fileName);
	else if (e.type == AssetType::Character)
		LoadCharacterPreview(state.character, e.relativePath, e.fileName);
	else if (e.type == AssetType::Effect)
		LoadFXPreview(state.fx, e.relativePath, e.fileName);
	else if (e.type == AssetType::Terrain)
		LoadTerrainPreview(state.terrain, e.relativePath, e.fileName);
}

// Offscreen render smoke (docs/ASSET_EXPLORER_PLAN.md, M1): brings up the GL
// backend, renders the test triangle into a render target and reads back the
// center pixel, asserting it is the drawn geometry rather than the clear color.
// Needs a GL context (run under Xvfb in CI). Returns 0 on success.
int RunRenderSmokeTest()
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		spdlog::error("AssetExplorer: SDL_Init failed: {}", SDL_GetError());
		return 1;
	}
	RHIDeviceGL::SetGLWindowAttributes();
	SDL_Window* pWindow = SDL_CreateWindow("AssetExplorer smoke", 320, 240,
		SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
	if (pWindow == nullptr)
	{
		spdlog::error("AssetExplorer: SDL_CreateWindow failed: {}", SDL_GetError());
		SDL_Quit();
		return 1;
	}
	auto* pRHI = new RHIDeviceGL(pWindow, /*bVSync=*/false);
	int rc = 1;
	if (pRHI->IsValid())
	{
		CN3Base::RHIDeviceSet(pRHI);
		ExplorerState state;
		state.rhi = pRHI;
		state.viewportW = 256;
		state.viewportH = 256;
		// Exercise the 3D path (orbit camera -> view/projection -> depth -> RT):
		// render the demo cube and read back the center, which the cube covers.
		state.shape.loaded   = true;
		state.shape.demoCube = true;
		state.shape.camera.FrameBounds(__Vector3(-1, -1, -1), __Vector3(1, 1, 1));
		RenderShapeToRT(state);

		uint8_t rgba[4] = {};
		const bool read = state.previewRT != nullptr
			&& pRHI->ReadRenderTargetPixel(state.previewRT, 128, 128, rgba);
		// The clear color is 0x14161B (dark); the triangle covers the center, so
		// a correct render reads back something clearly brighter.
		const int lum = rgba[0] + rgba[1] + rgba[2];
		std::printf("AssetExplorer render smoke: center rgba=(%d,%d,%d,%d) read=%d\n",
			rgba[0], rgba[1], rgba[2], rgba[3], read ? 1 : 0);
		rc = (read && lum > 96) ? 0 : 3;

		delete state.previewRT;
		CN3Base::RHIDeviceSet(nullptr);
	}
	delete pRHI;
	SDL_DestroyWindow(pWindow);
	SDL_Quit();
	return rc;
}

int RunSmokeTest(const std::string& dir)
{
	AssetIndex index;
	const std::size_t n = index.Scan(dir);
	const auto counts = index.CountsByCategory();
	std::printf("AssetExplorer smoke: %zu assets under %s\n", n, dir.c_str());
	for (AssetCategory cat : kFilterChips)
		std::printf("  %-6s %zu\n", AssetCategoryLabel(cat),
			counts[static_cast<std::size_t>(cat)]);
	return n > 0 ? 0 : 2;
}

} // namespace

int main(int argc, char** argv)
{
	std::string smokeDir;
	std::string dataOverride;
	bool renderSmoke = false;
	bool demoCube    = false;
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--smoke") == 0 && i + 1 < argc)
			smokeDir = argv[++i];
		else if (std::strcmp(argv[i], "--smoke-render") == 0)
			renderSmoke = true;
		else if (std::strcmp(argv[i], "--demo") == 0)
			demoCube = true; // show a synthetic 3D cube (no game data needed)
		else if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc)
			dataOverride = argv[++i];
		else if (argv[i][0] != '-')
			dataOverride = argv[i];
	}

	if (renderSmoke)
		return RunRenderSmokeTest();
	if (!smokeDir.empty())
		return RunSmokeTest(smokeDir);

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		spdlog::error("AssetExplorer: SDL_Init failed: {}", SDL_GetError());
		return 1;
	}

	// The engine's GL backend owns the context (matches the WarFare client). It
	// sets the GL window attributes, so do that before creating the window.
	RHIDeviceGL::SetGLWindowAttributes();

	SDL_Window* pWindow = SDL_CreateWindow("Knight OnLine - Asset Explorer", 1280, 800,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
	if (pWindow == nullptr)
	{
		spdlog::error("AssetExplorer: SDL_CreateWindow failed: {}", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	auto* pRHI = new RHIDeviceGL(pWindow, /*bVSync=*/true);
	if (!pRHI->IsValid())
	{
		spdlog::error("AssetExplorer: GL backend init failed");
		delete pRHI;
		SDL_DestroyWindow(pWindow);
		SDL_Quit();
		return 1;
	}
	CN3Base::RHIDeviceSet(pRHI);
	SDL_GLContext glContext = static_cast<SDL_GLContext>(pRHI->GLContext());

	ApplyWindowIcon(pWindow);
	SDL_ShowWindow(pWindow);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = nullptr;
	ApplyModernStyle();
	// Share the engine's GL context: ImGui records draw data and replays it into
	// the window framebuffer after the engine has rendered previews into RTs.
	ImGui_ImplSDL3_InitForOpenGL(pWindow, glContext);
	ImGui_ImplOpenGL3_Init("#version 330");

	ExplorerState state;
	state.rhi = pRHI;
	{
		const fs::path dir = FindGameDataDir(dataOverride);
		if (dir.empty())
		{
			std::error_code ec;
			state.dataDir = fs::current_path(ec);
			state.status = "No game-data directory found; pass --data <path>. "
						   "Showing current directory.";
			RescanDataDir(state, state.dataDir);
		}
		else
		{
			RescanDataDir(state, dir);
		}
	}

	if (demoCube)
	{
		// Bypass selection loading and pin the demo cube so the 3D viewport is
		// visible without any .n3shape asset present.
		state.loadedForSelection = -1;
		state.shape.loaded   = true;
		state.shape.demoCube = true;
		state.shape.partCount = 1;
		state.shape.triCount  = 12;
		state.shape.camera.FrameBounds(__Vector3(-1, -1, -1), __Vector3(1, 1, 1));
	}

	bool bRunning = true;
	while (bRunning)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			ImGui_ImplSDL3_ProcessEvent(&event);
			if (event.type == SDL_EVENT_QUIT
				|| (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
					&& event.window.windowID == SDL_GetWindowID(pWindow)))
				bRunning = false;
		}

		// Bring the preview in line with the current selection (loads a texture,
		// shape or character, or clears it), advance the animation timeline, then
		// render the 3D preview into the target using the panel size measured last
		// frame, before the UI references it.
		SyncPreview(state);
		const float dt = ImGui::GetIO().DeltaTime;
		if (state.character.loaded)
			state.character.player.Update(dt);
		RenderShapeToRT(state);
		RenderCharacterToRT(state);
		RenderFXToRT(state, dt);
		RenderTerrainToRT(state);

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		int windowW = 0, windowH = 0;
		SDL_GetWindowSize(pWindow, &windowW, &windowH);
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowW), static_cast<float>(windowH)));
		ImGui::Begin("##AssetExplorerRoot", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

		DrawToolbar(state);
		ImGui::Separator();

		// Body: list (fixed) | viewport (fills middle) | inspector (fixed),
		// all sharing one row and leaving space for the status bar below.
		const float listW      = 320.0f;
		const float inspectorW  = 300.0f;
		const float bodyH       = -ImGui::GetFrameHeightWithSpacing();
		DrawAssetList(state, ImVec2(listW, bodyH));
		ImGui::SameLine();
		DrawViewport(state, ImVec2(-(inspectorW + ImGui::GetStyle().ItemSpacing.x), bodyH));
		ImGui::SameLine();
		DrawInspector(state, ImVec2(0, bodyH));

		// Status bar.
		ImGui::Separator();
		ImGui::TextDisabled("%s", state.status.empty() ? "Ready" : state.status.c_str());

		ImGui::End();

		ImGui::Render();
		glViewport(0, 0, windowW, windowH);
		glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(pWindow);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	// Delete GL resources while the context is still current, then tear down the
	// backend (its destructor destroys the GL context).
	ReleaseTexturePreview(state.tex);
	ReleaseShapePreview(state.shape);
	ReleaseCharacterPreview(state.character);
	ReleaseFXPreview(state.fx);
	ReleaseTerrainPreview(state.terrain);
	delete state.previewRT;
	CN3Base::RHIDeviceSet(nullptr);
	delete pRHI;

	SDL_DestroyWindow(pWindow);
	SDL_Quit();

	return 0;
}
