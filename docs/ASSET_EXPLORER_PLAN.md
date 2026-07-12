# OpenKO Asset Explorer — Implementation Plan

A modern, cross-platform (macOS / Linux / Windows) tool to browse, inspect and
preview every asset the N3 engine can load: **textures, static shapes,
animated characters (with their animations/plugs), particle/FX bundles and
terrain/maps**. It supersedes the five original MFC tools
(`N3Viewer`, `N3TexViewer`, `N3CE`, `N3FXE`, `N3ME`) with a single Dear ImGui
application built on the same SDL3 + RHI foundation the POSIX client already
uses.

> Status: **planning**. This document is the design contract; nothing here is
> built yet. It follows the same conventions as `docs/PORT_POSIX_PLAN.md`:
> POSIX-first, never break the Windows build, `-Werror`, no protocol/asset
> changes, unit-tested core logic.

---

## 1. Goal & scope

| In scope | Out of scope (for v1) |
| --- | --- |
| Enumerate a game-data tree and list every loadable asset, grouped by type | Editing / re-authoring assets (that's what N3CE/N3FXE/N3ME did) |
| 2D preview of textures (`.dxt`, `.n3tex`, `.ksc`, `.tga`, `.bmp`) with mip/format/alpha inspection | Exporting to `.obj`/`.gltf` (nice-to-have, phase 7) |
| 3D preview of shapes and characters in an orbit-camera viewport | Terrain *editing* |
| Play character **animations**, switch **LOD**, attach **plugs** (weapons), scrub a **timeline** | Networked/live asset streaming |
| Preview **FX bundles** (particles, billboards, mesh trails) with play/loop/restart | |
| Load a **map/zone** (terrain + placed shapes) and fly around it | |
| Thumbnails, text search, type filters, recent files, favourites | |

The single hard technical prerequisite that unlocks all 3D previews is
**offscreen render-target support in the RHI** (§4). Everything else is
composition of code that already exists.

---

## 2. Asset inventory (real formats & loaders in this repo)

Every N3 asset is a `CN3BaseFileAccess` subclass with a virtual
`bool Load(File&)` / `bool LoadFromFile(const std::string&)`. Each serialized
object carries a type from the `OBJ_*` bitfield in
`src/N3Base/My_3DStruct.h`, which we use for **auto-detection**.

| Asset | Loader class | Header / file | Key API | Original tool |
| --- | --- | --- | --- | --- |
| Texture | `CN3Texture` (`src/N3Base/N3Texture.h`) | `.dxt`/`.n3tex`, DXT1-5 or raw; also `.tga`,`.bmp` | `LoadFromFile`, `Width/Height/PixelFormat/MipMapCount` | N3TexViewer |
| Encrypted texture | `KscViewerCore` (already ported) | `.ksc` = JPEG behind Borland cipher | `DecryptKsc`, `LoadImage` | KscViewer |
| Progressive mesh | `CN3PMesh` (`N3PMesh.h`) | `.N3PMesh` | vertices/indices/LOD | N3Viewer |
| Static shape | `CN3Shape` (`N3Shape.h`) | `.n3shape` (PMesh instance + texture refs) | `Load`, `Render`, `MeshInstance` | N3Viewer |
| Animated character | `CN3Chr` (`N3Chr.h`) | joints (`CN3Joint` ≤256), skins `CN3Skin[4 LOD]`, parts `CN3CPart`, plugs `CN3CPlug` | `Tick(fFrm)`, `TickAnimationFrame`, `TickJoints`, `Render(nLOD)`, `SetAnimation` | N3CE |
| Animation keys | `CN3AnimControl` / `CN3AnimKey` | `.N3Anim` | frame ranges, playback | N3CE |
| FX bundle | `CN3FXBundle` (`N3FXBundle.h`) + `CN3FXMgr` | parts: `CN3FXPartParticles`, `…BillBoard`, `…Mesh`, `…BottomBoard` | `Tick`, `Render`, life/move-type/pos | N3FXE |
| Terrain | `CN3Terrain` / `CN3TerrainManager` (`src/Client/WarFare/`) | `.gtd` game-terrain data | height/patches/texture layers | N3ME |
| World objects | `CN3ShapeMgr` | placed `CN3Shape` list | — | N3ME |
| Sky/env | `CN3Sky`,`CN3Cloud`,`CN3Sun`,`CN3Moon`,`CN3GERain`,`CN3GESnow` | — | ambience for map preview | — |

Resource caching/dedup is provided by the `CN3Mng<T>` template
(`src/N3Base/N3Mng.h`) — the explorer reuses it so a texture referenced by ten
shapes is loaded once.

---

## 3. Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  AssetExplorerMainSDL.cpp   (SDL3 window + ImGui frame loop)  │
│  ┌──────────────┬──────────────────────┬──────────────────┐  │
│  │ Asset tree    │  3D/2D Viewport       │  Inspector       │  │
│  │ + search/     │  (ImGui::Image of the │  (per-type       │  │
│  │  filter/      │   offscreen RT)       │   properties,    │  │
│  │  thumbnails   │  orbit camera + grid  │   anim timeline) │  │
│  └──────────────┴──────────────────────┴──────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│  AssetExplorerCore  (no ImGui, unit-tested)                  │
│   • AssetIndex      – walk data dir, classify, cache metadata │
│   • AssetType detect – ext + OBJ_* magic sniff                │
│   • Loader façade   – open any path → typed handle            │
│   • ScenePreview    – owns CN3Chr/CN3Shape/CN3FXBundle/terrain│
│   • OrbitCamera     – view/proj matrices, framing             │
├─────────────────────────────────────────────────────────────┤
│  N3Base engine  (CN3Texture, CN3Shape, CN3Chr, CN3FXBundle…) │
│  RHI (IRHIDevice)  + RHIDeviceGL / RHIDeviceSDLGPU           │
│   ►► NEW: offscreen render target (§4) ◄◄                    │
└─────────────────────────────────────────────────────────────┘
```

Guiding principle: **reuse the engine's own renderer**. We do not re-implement
mesh/skin/particle drawing — we drive `CN3Shape::Render()`,
`CN3Chr::Render(nLOD)`, `CN3FXBundle::Render()` and `CN3Terrain` exactly as the
game does, into an offscreen target, so previews are byte-for-byte what the
player sees. This is the single most important decision: it makes the tool a
*truthful* viewer and keeps it ~free to maintain as the engine evolves.

---

## 4. The one new engine capability: offscreen render targets

Today `IRHIDevice` (`src/N3Base/RHI/RHIDevice.h`) only presents to the window
swapchain. ImGui tools show 2D by handing a raw GL texture to `ImGui::Image`.
To show **3D**, the engine must render into a texture we can then sample.

Add a minimal render-to-texture surface to the RHI, implemented first in
`RHIDeviceGL` (GL is the default backend everywhere), then `RHIDeviceSDLGPU`,
with a no-op in `RHIDeviceNull` and the existing swapchain path in D3D9:

```cpp
// RHIDevice.h  (additions)
struct RHIRenderTargetDesc { uint32_t width, height; bool depth; };
class IRHIRenderTarget {                       // opaque handle
public:
    virtual ~IRHIRenderTarget() = default;
    virtual uint32_t Width()  const = 0;
    virtual uint32_t Height() const = 0;
    virtual void*    ColorHandle() const = 0;  // GLuint / SDL_GPUTexture* for ImGui
};
virtual IRHIRenderTarget* CreateRenderTarget(const RHIRenderTargetDesc&) = 0;
virtual void BeginRenderTarget(IRHIRenderTarget*) = 0;  // bind FBO + viewport + clear
virtual void EndRenderTarget() = 0;                      // restore default framebuffer
```

- **RHIDeviceGL**: FBO + color `GL_TEXTURE_2D` + depth renderbuffer. `BeginRenderTarget`
  binds the FBO, sets the GL viewport, clears; `EndRenderTarget` rebinds FBO 0 and
  restores the state ImGui's GL3 backend expects. `ColorHandle()` returns the
  `GLuint` cast to `ImTextureID` — same path KscViewer already uses.
- **RHIDeviceSDLGPU**: a `SDL_GPUTexture` used as a color target in its own
  render pass; hand the texture to ImGui's SDLGPU backend binding.
- **Null/D3D9**: trivial stub / existing surface API — Windows build stays green.

The frame loop becomes:

```
device->BeginRenderTarget(rt);
    scene.Render(camera);          // engine draws model/map/fx into rt
device->EndRenderTarget();
ImGui_NewFrame();
    ImGui::Image(rt->ColorHandle(), viewportSize);   // composited in the UI
ImGui_Render();  SDL_GL_SwapWindow();
```

This capability is independently useful: **thumbnail generation** (§8) and a
future in-game minimap/portrait renderer both want offscreen RTs.

---

## 5. Code layout

New tool under `src/Client/AssetExplorer/` (mirrors KscViewer's split of
testable core vs. SDL/ImGui shell):

```
src/Client/AssetExplorer/
  AssetType.h / .cpp          # enum + ext/magic detection  (unit-tested)
  AssetIndex.h / .cpp         # recursive scan, metadata cache, search/filter (unit-tested)
  AssetLoader.h / .cpp        # path -> typed handle façade  (unit-tested where headless)
  ScenePreview.h / .cpp       # owns the previewed object, Tick/Render via RHI
  OrbitCamera.h / .cpp        # view/proj, framing, input mapping (unit-tested math)
  AnimationPlayer.h / .cpp    # wraps CN3Chr anim state + timeline (unit-tested state)
  Thumbnailer.h / .cpp        # offscreen RT -> small RGBA + disk cache
  AssetExplorerMainSDL.cpp    # ImGui shell: tree / viewport / inspector, theming
  CMakeLists.txt
tests/AssetExplorer/
  AssetType_test.cpp
  AssetIndex_test.cpp
  OrbitCamera_test.cpp
  AnimationPlayer_test.cpp
```

RHI additions live in `src/N3Base/RHI/RHIDevice.h` + `src/Client/WarFare/RHIDeviceGL.*`
and `RHIDeviceSDLGPU.*` (where the GL/SDLGPU backends already are).

---

## 6. UX — "a modern, well-designed app"

Three-pane dockable layout (ImGui docking), dark theme reusing the palette the
Option/Launcher tools already ship, with a slim top toolbar and a status bar.

```
┌───────────── OpenKO Asset Explorer ─────────────────────────────────┐
│  [Open data dir▾] [🔍 search…]  Filter:[Tex][Shape][Chr][FX][Map]  ⚙ │  toolbar
├──────────────┬────────────────────────────────┬────────────────────┤
│ ASSETS        │            VIEWPORT             │  INSPECTOR         │
│ ▸ item/       │   ┌──────────────────────────┐  │  Name: gladiator   │
│ ▸ monster/    │   │   (orbit-camera 3D or     │  │  Type: Character   │
│   • orc.n3chr │   │    2D texture preview)    │  │  Verts: 3,214 LOD0 │
│   • orc.dxt   │   │   grid + axis gizmo        │  │  Joints: 42        │
│ ▸ npc/        │   └──────────────────────────┘  │  Anims: [Idle ▾]   │
│ ▸ terrain/    │   ◀▮▮▮▮───────────────▶  0:37/1:20│  ┌ Plugs ────────┐ │
│  [thumbnails] │   ▶ ⟳loop  speed[1.0x] LOD[0▾]   │  │ Weapon:[sword▾]│ │
│               │                                  │  └───────────────┘ │
├──────────────┴────────────────────────────────┴────────────────────┤
│ 1,842 assets · orc.n3chr loaded · GL · 3.1 ms/frame                  │  status
└─────────────────────────────────────────────────────────────────────┘
```

Design details that make it feel modern rather than a port of an MFC dialog:

- **Docking + layout presets** ("Texture", "Model", "Map") saved to `imgui.ini`.
- **Thumbnail grid mode** toggle for the asset list (icons rendered by the
  Thumbnailer, §8), with lazy/async generation and a disk cache.
- **Command palette** (Ctrl/⌘-P) for "open path", "frame selection", "reset camera".
- **Drag & drop** a file/folder onto the window to open it.
- **Instant search** with fuzzy match + type-facet chips; recent & favourites.
- **Checkerboard/black/white backdrops** and alpha toggle for textures.
- **Consistent iconography** (embedded icon font or the existing UI atlas).
- Keyboard-first: arrows navigate the tree, `F` frames, `space` play/pause anim.

---

## 7. Per-type functionality

### 7.1 Textures (`CN3Texture` / `.ksc`)
Decode to RGBA → upload once → `ImGui::Image`. Inspector shows dimensions,
`D3DFORMAT` (DXT1-5/raw), mip count, alpha presence, byte size. Controls: zoom
(fit / 1:1 / wheel), pan, mip-level selector, RGBA channel isolation, alpha
on/off, backdrop selector. Reuse `KscViewerCore` for `.ksc`. Batch **export to
PNG/JPEG** (single or whole folder).

### 7.2 Shapes (`CN3Shape`)
Load, resolve texture refs through `CN3Mng`, render in the orbit viewport.
Inspector: part count, triangle count, texture list (click a texture → open it
in the texture panel), bounding box. Toggles: wireframe, backface cull, normals,
bounding-box, per-part isolation.

### 7.3 Characters (`CN3Chr`) — the flagship feature
- **Animation player** (`AnimationPlayer`): enumerate the character's animations,
  drive `Tick`/`TickAnimationFrame`/`TickJoints`, expose a **scrub timeline**
  (current frame / total), play/pause/loop, speed multiplier, step frame.
- **LOD selector** (0-3) calling `Render(nLOD)`; show vert counts per LOD.
- **Plugs/weapons**: attach `CN3CPlug` items to joints (`m_Plugs`) — a dropdown
  to mount a sword/shield/bow onto the mount points, exactly as the game equips.
- **Skeleton overlay**: draw `m_MtxJoints` as a bone gizmo; hover a bone → name.
- Optional **cloak/color-change** (`CN3Cloak`, `CN3ColorChange`) toggles.

### 7.4 FX bundles (`CN3FXBundle`)
Instantiate through `CN3FXMgr`, `Tick(dt)`/`Render()` each frame in the viewport.
Controls: play / restart / loop, life scale, freeze, show emitters, per-part
enable (particles / billboard / mesh trail / bottom board). Inspector surfaces
`m_iMoveType`, `m_fLife0`, velocity, part breakdown. Great for QA of skill FX
(ties directly into the earlier "some skills have no animation / reload overlay
looks wrong" reports).

### 7.5 Maps / zones (`CN3Terrain` + `CN3ShapeMgr`)
Load a `.gtd` terrain + its placed shapes; switch the orbit camera to a **fly
camera** (WASD + mouse-look) to roam. Toggles: terrain texture layers,
wireframe, world-object visibility, sky/lighting (`CN3Sky` ambience). Minimap
inset. This is heavier — gate it behind its own milestone (§10, M6).

---

## 8. Cross-cutting systems

- **Asset index** (`AssetIndex`): recursive `std::filesystem` walk of the data
  dir (reusing `FindGameDataDir` from `Platform/GameDataDir.h`), classify each
  file, cache `{path, type, size, mtime}` to a sidecar so re-open is instant.
  Incremental rescan on focus. Search = case-insensitive fuzzy over relative path.
- **Type detection** (`AssetType`): fast path by extension; ambiguous/extensionless
  files are sniffed by reading the leading `OBJ_*` type word / KSC magic.
- **Thumbnailer**: render each 3D asset once into a small offscreen RT (§4),
  downscale to e.g. 96², cache PNGs under a cache dir keyed by path+mtime.
  Textures thumbnail directly. Generation is async on a worker thread, main
  thread only uploads the finished RGBA.
- **Orbit/Fly camera** (`OrbitCamera`): pure-math, unit-tested (yaw/pitch/dolly,
  `FrameBounds(aabb)` auto-fit). Maps mouse drag / wheel / WASD to transforms.
- **Error surface**: a load failure shows an inline banner with the reason
  (missing texture, bad magic, truncated file) instead of crashing — the index
  keeps working.

---

## 9. Dependencies & build

No new third-party dependencies beyond what the POSIX tools already vendor.
`CMakeLists.txt` mirrors `src/Client/KscViewer` + the WarFare RHI link set:

```cmake
add_executable(AssetExplorer
  AssetType.cpp AssetIndex.cpp AssetLoader.cpp ScenePreview.cpp
  OrbitCamera.cpp AnimationPlayer.cpp Thumbnailer.cpp
  AssetExplorerMainSDL.cpp)

target_link_libraries(AssetExplorer PRIVATE
  Platform SDL3::SDL3 imgui OpenGL::GL spdlog::spdlog shared
  N3Base                # the engine asset loaders
  WarFareRHI)           # RHIDeviceGL/SDLGPU + offscreen RT (extract to a lib, §Risks)
```

Gated behind `OPENKO_CLIENT_TOOLS_POSIX_EXPERIMENTAL` like the other tools.
Windows: an `if(WIN32)` arm can either build the same ImGui app against D3D9's
surface RT, or simply keep the legacy MFC tools — decided in M0. New presets:
none needed; builds under the existing `linux-clang-*` / `macos-arm64-*`.

---

## 10. Milestones (each independently shippable & pushed to `feature/port-posix`)

| # | Milestone | Deliverable | Unlocks |
| --- | --- | --- | --- |
| **M0** ✅ | Scaffolding | Empty ImGui window + `AssetType`/`AssetIndex` core + tests; tree lists & filters the data dir; **no preview yet** | the shell |
| **M1** ✅ | RHI offscreen RT | `CreateRenderTarget/Begin/End` on `IRHIDevice` (default no-op) + GL backend impl; a triangle rendered through the engine into the viewport's `ImGui::Image`; `--smoke-render` readback guard | all 3D |
| **M2** | Texture panel | Full `CN3Texture` + `.ksc` 2D preview, inspector, export | replaces N3TexViewer |
| **M3** | Shape viewport | `CN3Shape` orbit preview, wireframe/bbox, texture cross-links | replaces N3Viewer |
| **M4** | Character + animation | `CN3Chr` with anim timeline, LOD, plugs, skeleton | replaces N3CE |
| **M5** | FX bundles | `CN3FXBundle` playback controls | replaces N3FXE |
| **M6** | Maps | `CN3Terrain` + placed shapes, fly camera, minimap | replaces N3ME (view-only) |
| **M7** | Polish | Thumbnails, command palette, drag-drop, layout presets, SDLGPU RT path | the "modern app" feel |

M0-M3 deliver a genuinely useful tool (textures + static models); M4 is the
headline feature; M5-M7 round it out. Recommend building in this order and
pushing after each milestone.

---

## 11. Testing

- **Headless unit tests** (no display, run in CI like the existing 63):
  `AssetType` detection tables, `AssetIndex` scan/filter/search on a fixture
  tree, `OrbitCamera` math (framing, clamp), `AnimationPlayer` state machine
  (play/pause/loop/scrub bounds). Loader façade tested against tiny synthetic
  N3 headers where feasible; `.ksc` via the existing KscViewer fixtures.
- **Offscreen smoke test**: with a GL context under Xvfb, create an RT, render a
  known primitive, read back pixels, assert non-empty / expected clear colour —
  guards the M1 capability in CI.
- **Manual visual pass**: Xvfb + screenshot (the same `import`-based flow used
  during the client bring-up) against a real data dir.

---

## 12. Risks & mitigations

| Risk | Mitigation |
| --- | --- |
| RHI backends live in `src/Client/WarFare` and aren't a standalone lib | **M0 task**: extract `RHIDeviceGL/SDLGPU` (+ shared RHI glue) into a small `WarFareRHI` static lib linked by both WarFare and AssetExplorer. Pure refactor, no behaviour change. |
| SDL_GPU/Metal in-world corruption at scale (`docs` F6b) | Ship GL as the default RT backend (already the client default); SDLGPU RT is opt-in in M7. |
| GL state bleed between engine render and ImGui GL3 backend | `EndRenderTarget` restores the exact state ImGui expects; covered by the M1 smoke test. KscViewer already coexists ImGui + raw GL. |
| Some loaders assume a live device / `_N3GAME` globals | Initialise the same minimal engine globals the client does (`CN3Base::RHIDeviceSet`, `PathSet`); reuse `CN3Mng` caches. Character/terrain need the RHI device — available after M1. |
| Terrain (`.gtd`) is the heaviest and most game-coupled | Isolate to M6; view-only; fall back gracefully if a zone's dependencies are missing. |
| Windows build regressions | New RT methods get a D3D9 surface impl + Null stub; tool itself gated experimental; `-Werror` in CI catches drift. |

---

## 13. Future extensions (post-v1)

- Export: mesh → `.obj`/`.gltf`, animation → keyframe dump, texture atlas repack.
- Side-by-side **A/B compare** of two assets (diff textures / mesh stats).
- **Dependency graph**: "what shapes use this texture", "what FX this skill fires".
- Hot-reload watch: edit an asset externally → viewport refreshes.
- Fold into the client as an in-game debug asset inspector (F-key overlay).
