# Contexto para agentes: port POSIX del cliente OpenKO

> **Propósito de este documento:** que cualquier agente (o desarrollador) pueda
> retomar el port sin contexto previo. Léelo COMPLETO antes de tocar código.
> El roadmap detallado con tareas vive en `docs/PORT_POSIX_PLAN.md`; este
> documento contiene el conocimiento del repo, las reglas y los gotchas.
> Idioma del repo: código y comentarios nuevos en inglés; estos docs en español.

---

## 1. Objetivo y estado actual

**Objetivo:** que el cliente del juego (`src/Client/WarFare`, un MMORPG de
2002-2004 originalmente Win32 + Direct3D 9) compile y corra nativo en macOS
(prioridad) y Linux, **sin romper jamás el build de Windows** (MSBuild y CMake).

**Estado (fases 0-5 del plan completadas o casi):**

- ✅ F0: CMake configura el cliente en POSIX tras `-DOPENKO_CLIENT_POSIX_EXPERIMENTAL=ON`
  (presets en `CMakePresets.json`); CI `Client POSIX experimental` en
  `.github/workflows/build_cmake_all.yml` (ubuntu/clang + macos).
- ✅ F1: capa `src/Platform/` (tipos Win32/D3D9 shim, reloj chrono, encoding
  iconv CP949↔UTF-8, cripto SHA1/RC4, rutas case-insensitive en
  `src/FileIO/PathResolver`).
- ✅ F2: red sobre Asio con polling por tick (`CAPISocket::Poll()`), sin
  `WSAAsyncSelect`. Test de integración con servidor asio embebido.
- ✅ F3: entry point SDL3 (`WarFareMainSDL.cpp`), `CLocalInput` sobre SDL con
  tabla `SDL_Scancode → DIK_*`, decodificador de cursores `.cur`.
  **Verificado en un Mac real**: ventana abre, input se registra.
- ✅ F4: `CWinCrypt` portable; D3DX confinado a TUs Windows-only.
- 🔄 F5: RHI (`src/N3Base/RHI/`) con backends D3D9 (forwarder) y Null
  (headless). **~1.139 de ~1.560 llamadas al device migradas (73%)**. El
  subconjunto POSIX de N3Base compila: núcleo, audio completo, mallas,
  personajes, cielo, FX.

**Hito alcanzado:** ventana viva en macOS (pantalla negra: no hay backend con
píxeles aún). **Siguiente hito (C):** la escena de login renderizando en macOS.

## 2. Cómo compilar y testear (Linux o macOS)

```bash
cmake --preset linux-clang-debug -DOPENKO_BUILD_SERVERS=FALSE   # o macos-arm64-debug
cmake --build build/linux-clang-debug --parallel --target \
  shared FileIO MathUtils Platform N3Base_client JpegFile WarFare.Core WarFare \
  FileIO.Tests MathUtils.Tests Platform.Tests N3Base.Tests WarFareNet.Tests WarFareClient.Tests
cd build/linux-clang-debug && ctest --output-on-failure          # 6/6 suites
cd bin/Debug && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./KnightOnLine --smoke 30
```

- `OPENKO_BUILD_SERVERS=FALSE` evita la dependencia de unixODBC.
- El fetch de assets del juego está OFF en los presets
  (`OPENKO_FETCH_CLIENT_ASSETS=OFF`); actívalo solo si la tarea necesita
  assets reales (repo `Open-KO/ko-client-assets`, se clona en
  `build/<preset>/ClientData`).
- Todo compila con `-Werror`. Si tu cambio dispara un warning, arréglalo de
  raíz (no lo silencies con pragmas).

## 3. Mapa de los archivos que creó/tocó el port

| Área | Archivos | Qué son |
|---|---|---|
| PAL | `src/Platform/PlatformTypes.h`, `D3D9Types.h` | Shims **solo-POSIX** (en Windows no definen nada) de tipos de datos Win32/D3D9. Los valores D3D (`D3DFMT_*`, `D3DRS_*`, FVF) son los REALES de d3d9types.h — algunos van serializados en assets |
| PAL | `PlatformTime.h` (header-only), `PlatformString.h`, `PlatformEncoding.*`, `PlatformCrypto.*`, `PlatformPaths.h`, `DInputKeyCodes.h` | Reloj chrono, lowercase ASCII/`lstrcmpi`, CP949↔UTF-8 (iconv), SHA1+RC4, config dirs por SO, scancodes DIK para POSIX |
| RHI | `src/N3Base/RHI/RHIDevice.h` | Interfaz que calca el subconjunto de `IDirect3DDevice9` que usa el motor. **La API de render del motor** |
| RHI | `RHIDeviceD3D9.h` (Windows), `RHIDeviceNull.*` (portable), `RHIStateKey.h` | Backends: forwarder D3D9 (lo instala `CN3Eng`), Null headless (estados con round-trip, cuenta draws; lo instala el main SDL), clave de pipeline para la futura caché SDL_GPU |
| Entry | `src/Client/WarFare/WarFareMainSDL.cpp` | main SDL3 POSIX (Windows sigue con `WarFareMain.cpp`/WinMain). Flag `--smoke N` para CI headless |
| Input | `LocalInputSDL.{h,cpp}` | Backend SDL de `CLocalInput` (API pública intacta; tabla scancode→DIK) |
| Opciones | `GameOptions.{h,cpp}` | Carga de Option.ini compartida entre ambos entry points |
| Cursores | `CursorDecoder.{h,cpp}` | Parser .cur → RGBA (portable, testeado con los .cur del repo) |
| Red | `APISocket.{h,cpp}` | Reescrito sobre Asio (pimpl `CAPISocketTransport`); framing y JvCryption intactos |
| Tests | `tests/Platform`, `tests/N3Base`, `tests/WarFare` | gtest; incluyen server asio embebido (red) y smoke de carga `.n3vmesh` por el Null device |

Selección de fuentes por plataforma: `src/N3Base/CMakeLists.txt` →
`N3BASE_POSIX_PORTABLE_SOURCES` (crece con cada tarea);
`src/Client/WarFare/CMakeLists.txt` → rama `else()` de `WARFARE_CORE_SOURCES`.

## 4. REGLAS DE ORO (violarlas rompe Windows o los assets)

1. **Windows no se toca.** Todo cambio de comportamiento específico POSIX va
   tras `#ifdef _WIN32` / `#else`. La rama Windows debe quedar byte-idéntica.
   Excepciones ya consensuadas: el timer chrono, la red Asio y la RHI son
   multiplataforma a propósito (un solo camino de código).
2. **Si añades un `.cpp`/`.h` que Windows también compila, regístralo en el
   `.vcxproj` (y `.filters`) correspondiente además del CMakeLists.** El build
   preferido de Windows es MSBuild y NO lee CMake. Archivos solo-POSIX
   (p.ej. `*SDL.cpp`) NO van al vcxproj.
3. **Layout binario es sagrado.** `__Vector3`=12B, `D3DCOLOR`=u32 ARGB, los
   vértices/materiales tienen `static_assert` en `My_3DStruct.cpp`. Los shims
   D3D deben usar los valores numéricos reales (los assets serializan
   `D3DFORMAT`, y el protocolo de red no cambia NUNCA — regla del proyecto
   upstream).
4. **Protocolo y assets intactos** (regla de OpenKO upstream): nada de tocar
   el framing AA55/55AA, JvCryption, ni formatos `.n3*`/`.tbl`.
5. **Migración RHI = sustitución textual** `s_lpD3DDev->` → `RHIDevice()->`
   (o `CN3Base::RHIDevice()->` si estaba prefijado). Solo si TODOS los métodos
   que usa el archivo existen en `IRHIDevice`; si falta un método, o se añade
   a la interfaz (con impl en Null y D3D9) o el archivo espera.
6. **PCH**: `WarFare/StdAfx.h` incluye `winsock2.h` solo en `_WIN32` y debe ir
   ANTES de cualquier `windows.h`. No añadas headers de plataforma al PCH.
7. Los tests nuevos van en `tests/<área>` con gtest y se añaden al target list
   del job `Client POSIX experimental` del workflow si son un target nuevo.

## 5. Gotchas conocidos (te van a morder si no los sabes)

- `MAP_TYPE`: glibc lo filtra como macro vía boost::interprocess
  (`sys/mman.h`); `N3TableBase.h` ya hace `#undef`. Si aparece un error
  bizarro de "using declaration", busca colisiones de macros de libc.
- `CharLower` es DBCS-aware en Windows; en POSIX se usa `StrLowerAscii`
  (los paths en POSIX son UTF-8). No uses `tolower` por byte sobre CP949.
- Tokens D3D8 legacy (`D3DRS_ZBIAS`, `D3DTSS_ADDRESSU`...): el SDK vendorizado
  de Windows los tiene; el shim POSIX también, con su numeración histórica.
- El código de juego tiene strings/comentarios en coreano (UTF-8 en fuentes);
  los datos de assets son CP949 → usar `Cp949ToUtf8` en fronteras.
- Filesystem: los assets mezclan mayúsculas y separadores `\`;
  `FileReader::OpenExisting` ya resuelve ambos en POSIX. Escribir archivos
  aún no tiene resolución (F8 si hace falta).
- `s_pSocketSub` está semi-vestigial (nunca recibía datos con el WndProc
  viejo); con `Poll()` ahora sí se drena. No "arregles" cosas raras del
  legacy sin comprobar cómo funcionaba en Windows.
- gtest: cerrar un acceptor NO despierta un `accept()` bloqueante en otro
  hilo — conecta un cliente dummy para desbloquearlo (ver `APISocket_test`).
- El clang de CI (ubuntu-latest) y AppleClang difieren; si CI de macOS falla
  y Linux no, suele ser `-Wunused`/orden de includes.
- HiDPI/Retina: el motor entero trabaja en unidades LÓGICAS (s_Options, UI,
  XYZRHW, viewport, scissor, mouse); la ventana se crea con
  `SDL_WINDOW_HIGH_PIXEL_DENSITY` y los backends RHI escalan al framebuffer
  físico con `CN3Base::s_fPixelDensity` (GL: `TargetDensity()`; SDL_GPU:
  `m_fPixelDensity` en el replay). Render-to-texture NO se escala (density 1).
  DFont rasteriza el atlas a densidad física y encoge los quads
  (`m_fTextScale`). Si agregas un camino nuevo de coordenadas de pantalla,
  decide en qué unidad está y escala en el MISMO lugar que los existentes.
- Verifica siempre con: build completo + `ctest` + smoke (`--smoke 30`).

## 6. Qué NO hacer

- No introducir dependencias nuevas sin patrón `Find<X>.cmake`
  (sistema-primero + FetchContent pineado; ver `cmake/FindSDL3.cmake`).
- No renombrar APIs públicas de clases del motor (los ~40 TUs del juego las
  usan; el port es de plataforma, no un refactor).
- No convertir el juego a C++ "moderno" de paso: cambios mínimos y locales.
- No commitear a `master`; la rama de trabajo es `feature/port-posix`.
- No usar `sleep`/polling en tests: usa deadlines con timeout como los
  existentes.

## 7. Dónde está la verdad

- Roadmap y tareas: `docs/PORT_POSIX_PLAN.md` (checklists por fase; las fases
  6+ están subdivididas en tareas T-numeradas con criterio de aceptación).
- Decisiones de arquitectura y su porqué: sección 2.3 del plan.
- Estado de la migración RHI: `grep -c "RHIDevice()->"` vs
  `grep -c "s_lpD3DDev->"` en `src/N3Base src/Client/WarFare`.
- Los archivos que siguen en device directo y POR QUÉ: sección F5 del plan.
