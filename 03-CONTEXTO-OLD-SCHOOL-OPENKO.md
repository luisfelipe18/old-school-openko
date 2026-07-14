# 03 — Contexto de `old-school-openko/` (el port 12xx)

Este es el proyecto sobre el que ya se trabajó y la base de todo. Entiéndelo
bien antes de seguir. La verdad autoritativa está en sus propios documentos:

- `old-school-openko/docs/PORT_POSIX_PLAN.md` — plan de migración completo, por
  fases, con estado.
- `old-school-openko/docs/PORT_POSIX_CONTEXT.md` — reglas de oro, gotchas,
  cómo compilar y testear.

**Léelos.** Lo de abajo es un resumen para orientarte.

## Qué es

Un port del cliente **Knight Online 1298 (12xx)** —originalmente Windows-only—
a **macOS (Apple Silicon e Intel) y Linux**, manteniendo Windows compilando y
funcionando idéntico durante todo el proceso. Parte del código open-source del
motor.

## De qué dependía el cliente original (y cómo se resolvió)

El cliente estaba escrito contra APIs exclusivas de Windows en cinco capas. El
port introdujo dos capas de abstracción —**PAL** (plataforma) y **RHI**
(render)— para aislar todo:

| Capa | Original (Windows) | Reemplazo POSIX |
|------|--------------------|-----------------|
| Gráficos | Direct3D 9 + D3DX | **RHI** abstracto (ver backends abajo) |
| Ventana / loop | Win32 message loop | **SDL3** |
| Entrada | DirectInput 8 | **SDL3** (mapea a scancodes DIK_*) |
| Texto | GDI + control EDIT + IMM32 | **FreeType** (DFont) + SDL text input/IME |
| Red | Winsock (`WSAAsyncSelect`) | **Asio** |
| Audio | (ya portable) | OpenAL Soft + mpg123 |
| Matemática 3D | (ya migrada de D3DX) | `src/MathUtils` (portable) |

### Backends RHI (interfaz `IRHIDevice`)

- **RHIDeviceD3D9** — Windows, envuelve el `IDirect3DDevice9` real.
- **RHIDeviceGL** — OpenGL (Linux y fallback macOS).
- **RHIDeviceSDLGPU** — SDL_GPU sobre Metal/Vulkan; **default en macOS**.
- **RHIDeviceNull** — headless, para CI (ejecuta el render sin pixeles).

Todo el código de juego llama a `CN3Base::RHIDevice()->...` en vez de al
`s_lpD3DDev` de D3D9. Los tokens D3D (`D3DRS_*`, `D3DTSS_*`, `D3DTOP_*`, etc.)
existen como shims en `src/Platform/D3D9Types.h`.

## Estructura del repositorio

```
old-school-openko/
├── src/
│   ├── Platform/          # Shims Win32 → POSIX (LO MÁS REUSABLE, ver doc 04)
│   │   ├── D3D9Types.h         # tokens y structs de D3D9
│   │   ├── PlatformIni.h       # GetPrivateProfileString/Int (.ini)
│   │   ├── PlatformString.h    # lstrcpy/lstrcat/lstrcmpi, StrLowerAscii
│   │   ├── PlatformTime.h      # timeGetTime, Sleep, QueryPerformanceCounter
│   │   ├── DInputKeyCodes.h    # DIK_* scancodes
│   │   ├── PlatformTypes.h     # HWND/HINSTANCE/RECT/POINT/PostQuitMessage...
│   │   └── ...
│   ├── MathUtils/         # matemática 3D portable (ex-D3DX)
│   ├── FileIO/            # lectura de archivos, PathResolver (resuelve '\' y case)
│   ├── N3Base/            # motor 3D "Noah3D": render, texturas, mesh, UI, DFont
│   ├── Client/
│   │   ├── WarFare/       # EL CLIENTE (KnightOnline). Punto de entrada, UIs, lógica
│   │   ├── Option/        # herramienta Option (ImGui en POSIX)
│   │   ├── KscViewer/     # visor de .ksc/.jpg cifrados (ImGui)
│   │   ├── Launcher/      # launcher (ImGui): version check + lista servidores
│   │   └── AssetExplorer/ # explorador de assets del juego
│   ├── Server/            # SERVIDORES 12xx, ya portados a Linux
│   │   ├── Ebenezer/      # game server principal
│   │   ├── AIServer/      # IA de monstruos
│   │   ├── VersionManager/, ItemManager/, Aujard/
│   │   └── shared-server/ # utilidades compartidas servidor
│   └── shared/            # compartido cliente+servidor (incluye JvCryption, cifrado)
├── tests/                 # gtest: N3Base, WarFare, servidores, etc.
├── docs/                  # PORT_POSIX_PLAN.md, PORT_POSIX_CONTEXT.md, y más
├── CMakeLists.txt
└── CMakePresets.json
```

## Cómo se compila

Usa CMake con presets (`CMakePresets.json`). En Linux/macOS los presets del
cliente activan `OPENKO_CLIENT_POSIX_EXPERIMENTAL`:

```
# Linux
cmake --preset linux-clang-debug
cmake --build build/linux-clang-debug -j

# macOS
cmake --preset macos-arm64-debug
cmake --build build/macos-arm64-debug -j
```

Presets: `macos-arm64-debug/release`, `linux-clang-debug/release`,
`linux-gcc-debug/release`, `linux-asan`.

> **IMPORTANTE — el gate de plataforma**: el `CMakeLists.txt` raíz fuerza
> `OPENKO_BUILD_CLIENT OFF` si no es `WIN32`, salvo que se use un preset POSIX
> del cliente (que define `OPENKO_CLIENT_POSIX_EXPERIMENTAL`). Si el target
> WarFare "no aparece", es porque no se está usando el preset correcto.

> **`warfare_config.h`**: hace falta copiar `warfare_config.h.default` →
> `warfare_config.h` (el CMake lo hace). Define `LOGIN_SCENE_VERSION` (1098 o
> 1298).

## Verificación estándar

Siempre: **build completo + `ctest` + smoke headless**:

```
cmake --build build/linux-clang-debug -j
cd build/linux-clang-debug && ctest --output-on-failure
SDL_VIDEODRIVER=dummy ./bin/Debug/KnightOnLine --smoke 30
```

## Estado alcanzado

- **Hito D ("jugable")**: login → mundo validado en un Mac real contra un
  servidor Ebenezer 12xx local. Login, lista de servidores, selección de
  nación/personaje, creación de personaje, entrada al mundo, movimiento,
  cámara, UI in-game funcionando.
- **Hito E ("distribuible")**: empaquetado con CPack (cliente + servidor),
  workflow de release, para las tres plataformas.
- **Herramientas cliente** (Option, KscViewer, Launcher, AssetExplorer)
  portadas a POSIX con Dear ImGui (SDL3+GL3).
- **Servidores 12xx** compilan y corren en Linux (usan **nanodbc** para ODBC).

## Ramas de git (al momento del handoff)

- `master` — rama por defecto. `feature/port-posix` ya se mergeó aquí y se
  borró.
- `feature/add-shinning` — última rama de trabajo. Incluye: brillo de ítems
  upgradeados (+7..+10), fuente "variante E" (sin hinting + alfa 8-bit),
  soporte HiDPI/Retina nativo, y un toggle en tiempo real entre las dos
  pantallas de login (1098/1298).
- `feature/contextos` — **esta rama**: solo estos documentos.

## Trabajo reciente relevante (rama `feature/add-shinning`)

Contexto de features añadidas en la última sesión, por si tocas esas zonas:

1. **Brillo de ítems upgradeados**: en `N3Chr.cpp` (`CN3CPart::Render` /
   `CN3CPlugBase::Render`) se ilumina el objeto (emissive + MODULATE2X) según
   `m_iGlowLevel`, derivado en `CGameBase::ItemUpgradeGlowLevel()` del nivel de
   upgrade (`dwID % 10`, múltiplos de 10 = +10). El wiring vive en
   `CPlayerBase::PartSet/PlugSet` y, ojo, `CPlayerMySelf` **sobrescribe** esos
   métodos sin llamar a la base (por eso el glow se setea también ahí, en el
   modelo del mundo y el del inventario `m_ChrInv`).
2. **Fuentes (DFont)**: rasterización `FT_LOAD_NO_HINTING` + textura de texto
   a `A8R8G8B8` (antes `A4R4G4B4`, que bandeaba los bordes).
3. **HiDPI/Retina**: ventana con `SDL_WINDOW_HIGH_PIXEL_DENSITY`; el motor
   trabaja en unidades **lógicas** y los backends RHI escalan al framebuffer
   físico con `CN3Base::s_fPixelDensity` (GL: `TargetDensity()`; SDL_GPU:
   `m_fPixelDensity` en el replay). DFont rasteriza el atlas a densidad física.
4. **Toggle de login**: ambas variantes (1098/1298) se compilan e instancian
   siempre; `CGameProcedure::ToggleLoginVariant()` cambia entre ellas en
   caliente. Un recuadro auto-dibujado abajo-izquierda del login lo activa.

## Gotchas importantes (del PORT_POSIX_CONTEXT.md — revisa el original)

- Todo little-endian (Windows x64, macOS, Linux). `ByteBuffer`/`Packet`
  serializa con `memcpy` byte a byte — **no hay `htons`/`ntohl`** en ningún
  lado. Ver doc 05.
- Lecturas desalineadas eran UB en ARM: se corrigieron con `memcpy` en
  `ByteBuffer::read<T>`, `APISocket`, `JvCryption` y las funciones legacy
  `GetShort/GetInt/GetDWORD` del servidor. **No reintroduzcas
  `*(uint16_t*)ptr`** sobre offsets arbitrarios.
- Strings: código de juego en coreano (UTF-8 en fuentes), datos de assets en
  **CP949**. Convertir en fronteras (`Cp949ToUtf8`, `LocalToNet`/`NetToLocal`).
- Filesystem: assets mezclan mayúsculas y separadores `\`;
  `FileReader::OpenExisting` / `PathResolver` ya resuelven ambos en POSIX.
- No rompas Windows: el port mantiene `#ifdef _WIN32` para el camino nativo.
