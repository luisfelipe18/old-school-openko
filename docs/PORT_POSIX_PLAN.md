# Plan de implementación: port del cliente WarFare a plataformas POSIX (macOS / Linux)

> **Estado:** propuesta / borrador de trabajo
> **Rama:** `feature/port-posix`
> **Objetivo primario:** compilar y ejecutar el cliente (`src/Client/WarFare`) de forma nativa en **macOS** (Apple Silicon e Intel), manteniendo soporte **Linux** en el mismo esfuerzo y sin romper el build de Windows en ningún momento.

---

## 1. Resumen ejecutivo

El cliente está escrito contra APIs exclusivas de Windows en cinco capas: gráficos
(Direct3D 9 + D3DX), ventana/bucle de mensajes (Win32), entrada (DirectInput 8),
texto (GDI + control `EDIT` + IMM32) y red (Winsock en modo `WSAAsyncSelect`).
El audio **ya es multiplataforma** (OpenAL Soft + mpg123) y la matemática 3D
**ya fue migrada** de D3DX a `src/MathUtils` (portable). Eso significa que el
trabajo pesado restante se concentra en render, ventana/input y texto.

La estrategia es **incremental y por capas**, introduciendo una capa de
abstracción de plataforma (PAL) y un renderer abstracto (RHI), de modo que:

1. Windows siga compilando y funcionando idéntico durante todo el port.
2. Cada fase tenga un criterio de aceptación verificable en CI.
3. El primer objetivo visible sea la escena de login renderizando en macOS.

Stack elegido (disponible vía Homebrew / apt en todas las distros objetivo):

| Subsistema | Hoy (Windows) | Reemplazo POSIX | Paquete |
|---|---|---|---|
| Ventana + bucle de eventos | Win32 (`WinMain`, `WndProc`) | **SDL3** | `brew install sdl3` / `libsdl3-dev` |
| Entrada teclado/ratón | DirectInput 8 (`DIK_*`) | **SDL3** (tabla de mapeo scancode→DIK) | ídem |
| Gráficos | Direct3D 9 fixed-function | **RHI propia** + backend **OpenGL 3.3/4.1 core** para el bring-up, y **SDL_GPU** (Metal en macOS, Vulkan en Linux) como backend definitivo | GL incluido; `glad` vendorizado; SDL_GPU viene con SDL3 |
| Fuentes | GDI (`CreateFont`/`ExtTextOut`) | **FreeType** (atlas de glifos a textura) | `brew install freetype` / `libfreetype-dev` |
| Entrada de texto / IME | control Win32 `EDIT` + IMM32 | **SDL3 text input** (`SDL_StartTextInput`, eventos `SDL_EVENT_TEXT_*`) | ídem SDL |
| Red | Winsock `WSAAsyncSelect` + `WM_SOCKETMSG` | **Asio standalone** (ya vendorizado en `deps/asio`, lo usa el servidor) | ya en el repo |
| Audio | OpenAL Soft + mpg123 (ya portado) | sin cambios; compilar deps en POSIX | `openal-soft`, `mpg123` |
| Codificación de texto | codepage ANSI/CP949 implícita | **UTF-8 interno + iconv** en fronteras (assets/red) | `iconv` (libc) |
| Timing | `timeGetTime()` (winmm) | `std::chrono::steady_clock` | estándar |
| Empaquetado | `.rc`, `.ico`, `.cur`, exe Win32 | bundle `.app` + `Info.plist` + `.icns` (macOS), `.desktop` (Linux) | CMake `MACOSX_BUNDLE` |

Herramientas de desarrollo en las plataformas objetivo: CMake ≥ 3.26 + Ninja,
Apple Clang / clang / gcc, `lldb`, sanitizers (ASan/UBSan), RenderDoc (Linux) y
Xcode GPU Frame Capture + Instruments (macOS), `ccache` para CI.

---

## 2. Inventario del estado actual (auditoría)

### 2.1 Ya portable (no requiere trabajo de diseño)

* **Matemática 3D:** `src/MathUtils` (`__Vector3`, `__Matrix44`, `__Quaternion`,
  `_IntersectTriangle`, …) es C++ puro. Del viejo D3DX solo sobreviven ~8
  utilidades (ver 2.2, punto g).
* **Audio:** `src/N3Base` (AudioThread/AudioAsset/N3SndMgr) usa OpenAL Soft +
  mpg123; `deps/` ya tiene wrappers de build no-MSVC (`openal-soft`, `mpg123`)
  porque existen variantes `-msvc` separadas.
* **Red a nivel de bytes:** `shared/` (packets, `JvCryption`,
  `ExpandableCircularBuffer`, `Ini`) compila en POSIX — lo usa el servidor, que
  ya compila en Ubuntu 24.04 y macOS 15 según el README y los workflows.
* **FileIO:** `src/FileIO` (FileReader/FileWriter) también lo consume el
  servidor; requiere solo la capa de resolución case-insensitive (ver 2.2.h).
* **CMake:** todo el árbol de cliente ya tiene CMakeLists (no solo vcxproj);
  el bloqueo es el force-OFF de `OPENKO_BUILD_CLIENT` en no-Windows
  (`CMakeLists.txt` raíz, líneas 58-63).

### 2.2 Dependencias Windows-only a eliminar

| # | Dependencia | Dónde vive | Alcance |
|---|---|---|---|
| a | Direct3D 9 core (`LPDIRECT3DDEVICE9`, render states, texturas, VB/IB, `Present`) | `N3Base` completo; 38 headers exponen tipos D3D | **Alta** — corazón del port |
| b | Win32 ventana + message loop (`WinMain`, `WndProc`, `WM_*`, `PeekMessage`, `ChangeDisplaySettings`) | `WarFareMain.cpp`, `N3Eng.cpp`, `GameEng.cpp` | Media |
| c | DirectInput 8 (`dinput.h`, `LPDIRECTINPUT8`, scancodes `DIK_*`) | `LocalInput.*` + ~150 usos de `DIK_*` en 39 archivos | Media (mitigable con tabla de mapeo) |
| d | Winsock asíncrono (`WSAStartup`, `WSAAsyncSelect`, `ioctlsocket`, `WM_SOCKETMSG`) | `APISocket.cpp`, `WarFareMain.cpp`; `<winsock2.h>` en el PCH `StdAfx.h` | Media |
| e | GDI para fuentes (`CreateFont`, `HDC`, `ExtTextOut`) | `N3Base/DFont.cpp` | Media |
| f | Control `EDIT` nativo + IMM32 (`ImmGetContext`, …) | `N3Base/N3UIEdit.cpp`, `WM_COMMAND`/`EN_CHANGE` en `WarFareMain.cpp` | Media |
| g | Utilidades D3DX residuales: `D3DXLoadSurfaceFromSurface` (8), `D3DXCreateTextureFromFileEx` (1), filtros `D3DX_FILTER_*` | `N3Texture.cpp`, `DFont.cpp` | Baja (reemplazo puntual) |
| h | Convenciones de plataforma: `timeGetTime` (17 usos), `lstrcpy/lstrcat`, `_MAX_PATH`, `GetCurrentDirectory`, `GetCursorPos`/`SetCursorPos`, `MessageBox`, tipos `HWND/DWORD/POINT/RECT/BOOL` en interfaces, rutas case-insensitive, strings en codepage ANSI/CP949 | transversal | Media (mecánica, mucha superficie) |
| i | Recursos embebidos (`Resource.rc`, `.ico`, `.cur`, aceleradores) | `WarFare/` | Baja |

### 2.3 Decisiones de diseño clave (y su justificación)

1. **SDL3 y no GLFW/Qt:** SDL cubre en una sola dependencia ventana, eventos,
   input con scancodes estables, soporte IME (crítico: el juego acepta texto
   coreano), cursores de color, portapapeles, message boxes y gestión de
   fullscreen/DPI. GLFW no trae IME maduro ni cursores animados; Qt es
   desproporcionado. Si se prefiere máxima estabilidad en distros viejas,
   SDL2 es un fallback directo (la API usada será la común).
2. **OpenGL core como backend de *bring-up*, SDL_GPU como backend definitivo:**
   el pipeline del juego es fixed-function D3D9
   (`SetRenderState`/`SetTextureStageState`) con estados que cambian por draw
   call; su emulación con 1 shader über parametrizado mapea casi 1:1 a GL 3.3
   sin necesidad de caché de pipelines — es la ruta más corta al primer píxel.
   Matices honestos de GL en macOS: está congelado en 4.1, deprecado, y **no
   tiene depurador gráfico** (Xcode eliminó el frame capture de GL y RenderDoc
   no existe en macOS); la mitigación es depurar el backend GL en Linux con
   RenderDoc (es el mismo código) y en macOS solo ejecutarlo. Por eso el
   backend definitivo para macOS será **SDL_GPU** (fase 6b): viene incluido en
   SDL3 (cero dependencias nuevas), usa Metal nativo en macOS (con Xcode GPU
   capture funcional) y Vulkan en Linux. Su costo — pipelines precompilados
   (requiere una caché de pipeline-objects en la RHI) y shaders offline vía
   `SDL_shadercross` — se paga una sola vez porque la RHI (fase 5) se diseña
   desde el día uno con clave-de-pipeline explícita. Se evaluó y descartó:
   bgfx (modelo de submission muy afín a D3D9 pero dependencia grande con
   toolchain propio `shaderc`; queda como plan B), Vulkan+MoltenVK a mano
   (boilerplate desproporcionado), WebGPU nativo Dawn/wgpu-native (historia
   nativa en C++ aún inestable), ANGLE (build con depot_tools/gn impracticable
   de empaquetar), Diligent (backend Metal bajo licencia comercial) y
   DXVK-native (solo Linux, no cubre macOS).
3. **Asio para la red en TODAS las plataformas (Windows incluido):** en lugar de
   `#ifdef`s sobre Winsock, `CAPISocket` pasa a socket TCP no bloqueante de Asio
   con polling explícito una vez por frame desde el game loop. Esto elimina el
   acople socket→cola de mensajes de ventana (`WM_SOCKETMSG`) y de paso
   simplifica Windows. `deps/asio` ya existe y el equipo ya lo conoce por el
   servidor.
4. **Conservar los códigos `DIK_*` como enum propio:** hay ~150 usos en lógica
   de juego. En vez de tocarlos todos, `CLocalInput` se reimplementa sobre SDL
   y una tabla `SDL_Scancode → DIK` conserva la API pública intacta. Cero
   cambios en gameplay.
5. **UTF-8 interno, conversión en las fronteras:** los assets (.tbl, UIs,
   strings) y el protocolo usan CP949/ANSI. La regla será: memoria = UTF-8;
   al leer/escribir assets y paquetes de red se convierte con `iconv`
   (POSIX nativo, disponible en macOS y glibc). En Windows se mantiene el
   comportamiento actual hasta que la conversión esté probada.
6. **No romper Windows nunca:** cada PR del port debe pasar el workflow de
   Windows existente. Los `#ifdef` se concentran en `src/Platform/` (nuevo);
   el código de juego no debe ganar `#ifdef`s dispersos.

---

## 3. Fases de implementación (ordenadas por dependencia)

> Las fases 0–2 no producen nada visible pero desbloquean todo lo demás.
> El orden es estricto: cada fase asume la anterior integrada.
> Estimaciones en semanas-persona (sp), asumiendo 1 dev con experiencia media
> en gráficos.

### Fase 0 — Build system y CI (esfuerzo: ~1 sp)

**Objetivo:** que `cmake -B build` en macOS/Linux configure el cliente (aunque
aún no compile completo) y que CI vigile la regresión.

* [x] `CMakeLists.txt` raíz: sustituir el force-OFF de `OPENKO_BUILD_CLIENT`
      en no-Windows por una opción experimental
      `OPENKO_CLIENT_POSIX_EXPERIMENTAL` (default OFF) que permita activarlo.
      El fetch del `dx9sdk` quedó además condicionado a `WIN32`.
* [x] Presets de CMake (`CMakePresets.json`): `macos-arm64-{debug,release}`,
      `linux-clang-{debug,release}`, `linux-gcc-{debug,release}` (generador
      Ninja, flag experimental ON, assets OFF por defecto).
* [x] Integrar deps POSIX: verificado en Linux que `asio`, `mpg123`,
      `openal-soft` y `jpeg` configuran con el cliente activado; añadido
      `cmake/FindSDL3.cmake` (sistema primero, fallback a FetchContent con
      SDL 3.2.30 pineado). `freetype` y `glad` se difieren a las fases que
      los consumen (F7 y F6) para no arrastrar dependencias muertas.
* [x] GitHub Actions: job `Client POSIX experimental` (ubuntu-latest/clang +
      macos-latest/AppleClang) en `build_cmake_all.yml`, que compila los
      targets portables (`shared`, `FileIO`, `MathUtils` + tests) y ejecuta
      ctest. El job crece fase a fase. (`JpegFile` aún requiere `windows.h`
      → entra en F1.)
* [x] Documentar setup en el README (sección "Experimental: POSIX client
      port"): `brew install cmake ninja sdl3` / `apt install build-essential
      clang cmake ninja-build`; el resto de deps se auto-obtienen por
      FetchContent.

**Aceptación:** CI verde en 3 SOs con los targets portables actuales;
Windows intacto.

### Fase 1 — Fundaciones de portabilidad, sin nada visual (esfuerzo: ~2 sp)

**Objetivo:** que `N3Base` (menos render/UI) y la lógica no visual de
`WarFare.Core` compilen en macOS con clang. Es la fase "mecánica" grande.

* [x] Crear `src/Platform/` con:
  * `PlatformTypes.h`: no-op en Windows; en POSIX define los tipos de datos
    Win32 que se filtran por interfaces (`BOOL`, `DWORD`, `POINT`, `RECT`,
    `COLORREF`, `HRESULT`, handles opacos `HWND`/`HINSTANCE`).
  * `D3D9Types.h`: definiciones POSIX binario-compatibles de los tipos de
    *datos* D3D9 que viven en estructuras y formatos del juego (`D3DCOLOR`,
    `D3DCOLORVALUE`, `D3DMATERIAL9`, `D3DLIGHT9`, `D3DFORMAT` con los valores
    reales, FVF, blend/texture-op); las interfaces COM quedan como punteros
    opacos. `My_3DStruct.h` lo incluye en lugar de `<d3dx9.h>` fuera de
    Windows, y `My_3DStruct.cpp` congela los layouts con `static_assert`
    (vértices, materiales, `__Vector3`==12 bytes) en *todas* las plataformas.
  * `PlatformTime.h`: `PlatformTickMs()`/`PlatformTimeSeconds()` sobre
    `std::chrono::steady_clock`; migrados los 13 usos reales de
    `timeGetTime()` (N3SkyMng, LocalInput) y reescritos `CN3Base::TimeGet` y
    `TimerProcess` (antes QPC + fallback) en una única versión chrono.
  * `PlatformString.h`: `StrLowerAscii` para los `CharLower` (Windows
    conserva `CharLower`, que es DBCS-aware). El barrido de
    `lstrcpy/lstrcat/wsprintf` (~19 usos, todos en código WarFare que se
    reescribe en F2/F3) queda para esas fases.
  * `PlatformDebug.h` resultó innecesario: `DebugUtils.h` ya era portable
    (fmt + assert, `OutputDebugString` ya gateado).
* [x] **Sacar `<winsock2.h>` del PCH** (`WarFare/StdAfx.h`): gateado a
      `_WIN32` (en Windows debe preceder a `<windows.h>`; POSIX obtiene la
      red con Asio en F2).
* [ ] `GetCurrentDirectory`/`Option.ini` a `std::filesystem` + directorios de
      config de usuario por SO — se hace junto con el `main` SDL en F3
      (`CN3Base::PathSet` ya es portable: separador y lowercase por
      plataforma).
* [x] **Resolución case-insensitive de assets**: `FileIO/PathResolver`
      (`ResolveCaseInsensitivePath` + normalización de separadores `\` → `/`)
      integrado en `FileReader::OpenExisting` en POSIX, con tests. (Caché de
      directorio pendiente si el profiling lo pide.)
* [x] **Capa de encoding**: `Platform/PlatformEncoding` —
      `Cp949ToUtf8`/`Utf8ToCp949` sobre iconv (POSIX, con alias
      CP949/UHC/EUC-KR) y WideCharToMultiByte (Windows), con tests de
      round-trip en coreano.
* [x] Barrido Win32 en el subconjunto portado: `MessageBox` (N3SndMgr,
      N3BaseFileAccess), `GetLocalTime`/`SYSTEMTIME` (LogWriter → `std::tm`),
      `GetAsyncKeyState` (`_IsKeyDown` → stub POSIX hasta F3), render de
      debug D3D gateado (RenderLines, RenderCollisionMesh). El barrido del
      código de juego WarFare llega con F2/F3.
* [x] CMake: `N3Base_client` compila en POSIX desde
      `N3BASE_POSIX_PORTABLE_SOURCES` (core + formatos + anim + tablas +
      **audio OpenAL/mpg123 completo**); las fuentes de render/UI/cielo se
      reincorporan en F5–F7. Libs `d3d9/dinput8/...` gateadas a Windows.
      **Excluido `WinCrypt`** (CryptoAPI): las texturas cifradas necesitan
      una reimplementación portable de su derivación RC4 — movido a F4.

**Aceptación (estado):** en Linux/clang compilan `MathUtils`, `FileIO`,
`shared`, `Platform`, `JpegFile` (mitad GDI gateada a Windows) y el
subconjunto no-render de `N3Base` con `-Werror`; tests de FileIO (incl.
resolvedor), MathUtils y Platform (encoding/reloj) en verde; el layout
binario queda protegido por `static_assert` en todas las plataformas. macOS
se valida con el job de CI. El test de humo con un `.n3chr` real se movió a
F4 (requiere `N3Texture`/meshes portados y assets en CI); `ZipArchive` se
descartó del alcance (es de las client tools, que siguen siendo
Windows-only).

### Fase 2 — Red: de `WSAAsyncSelect` a Asio (esfuerzo: ~1.5 sp)

**Objetivo:** `CAPISocket` multiplataforma y desacoplado de la ventana.

* [x] Reescribir `CAPISocket` (`Connect/Disconnect/Send/Receive/ReConnect`)
      sobre `asio::ip::tcp::socket` no bloqueante (transporte pimpl
      `CAPISocketTransport`, así los ~40 TUs que incluyen `APISocket.h` no
      arrastran asio); API pública, framing (AA55/55AA), buffers y
      criptografía (`JvCryption`) intactos — el protocolo NO cambia. El
      resolver de asio reemplaza `inet_addr`/`gethostbyname`, y el `send()`
      parcial que reenviaba el buffer completo desde el inicio (bug latente)
      quedó corregido con `write_some` + reintento por escritura.
* [x] Sustituir el modelo push (`WM_SOCKETMSG` → `WndProc`) por polling:
      `CAPISocket::Poll()` llamado una vez por tick desde el pump de red de
      `CGameProcedure` drena lecturas y devuelve FALSE al detectar la
      desconexión (→ `ReportServerConnectionClosed(true)`, como hacía
      `FD_CLOSE`). El socket secundario ahora también se drena (antes el
      WndProc solo atendía al principal). El error de envío ya no hace
      `PostQuitMessage(-1)`: se reporta por el mismo camino de desconexión.
* [x] Eliminar `WM_SOCKETMSG` de `WarFareMain.cpp`/`APISocket.h` y
      `WSAStartup`/`WSACleanup`/`ioctlsocket` (asio inicializa Winsock).
* [x] Adoptar la misma implementación en Windows (un solo camino). MSBuild:
      `WarFare(.Core).vcxproj` importan `asio.props` y `Client.slnx`/
      `All.slnx` añaden la dependencia `fetch-asio`. Extra necesario:
      `GameDef.h` ya no exige `<dinput.h>` fuera de Windows — los scancodes
      `DIK_*` canónicos viven en `Platform/DInputKeyCodes.h` (base para el
      mapeo SDL de F3).

**Aceptación (estado):** `WarFare.Core` compila en POSIX (subconjunto de
red) y nuevo test de integración `WarFareNet.Tests` con un servidor asio
embebido que habla el framing real: conexión fallida, recepción en claro,
frame de envío bien formado, **recepción cifrada JvCryption** y detección de
desconexión vía `Poll()` — 5/5 en verde en Linux/clang; macOS vía CI. El
handshake contra Ebenezer/VersionManager reales queda como validación
manual/side-by-side (requiere base de datos SQL que CI no provee).

### Fase 3 — Ventana, bucle principal y entrada con SDL3 (esfuerzo: ~2 sp)

**Objetivo:** ventana abierta y entrada funcionando en macOS/Linux; game loop
portado. (Aún sin render: pantalla negra con clear color.)

* [x] Nuevo `WarFareMainSDL.cpp`: `main()` + `SDL_CreateWindow` (high-DPI),
      modo ventana/fullscreen desde `Option.ini`
      (`SDL_SetWindowFullscreen`), toggle Alt+Enter, y flag `--smoke N` que
      corre todo el stack headless con el driver de video *dummy* (usado por
      CI). La carga de opciones se extrajo de `WinMain` a
      `GameOptions.cpp` (compartida por ambos entry points; cierra además el
      pendiente de F1: `std::filesystem` + `Option.ini` por-usuario en
      `~/Library/Application Support/OpenKO` / `$XDG_CONFIG_HOME/openko` vía
      `Platform/PlatformPaths.h`).
* [x] Bucle: `SDL_PollEvent` + tick en idle replicando el patrón
      `PeekMessage`. Eventos mapeados: foco (`SDL_EVENT_WINDOW_FOCUS_*`),
      cierre (`SDL_EVENT_QUIT`/`WINDOW_CLOSE_REQUESTED`), rueda con delta en
      unidades `WHEEL_DELTA`. El enrutado final a
      `RequestExit`/UI/`CameraZoom` queda marcado con `TODO(F6+)` en los
      handlers — requiere que `CGameProcedure` compile (fases RHI); por
      ahora el tick es un pase de diagnóstico que loguea DIK/ratón/foco.
* [x] `CLocalInput` reimplementado sobre SDL (`LocalInputSDL.cpp`): tabla
      `SDL_Scancode → DIK_*` (~130 teclas, valores dinput exactos), misma
      máquina de estados press/released/doble-clic/drag que la versión
      DirectInput, `0x80` en tecla retenida como DirectInput. **API pública
      intacta** (el header solo gatea los miembros DirectInput); Windows
      sigue usando `LocalInput.cpp` sin cambios.
* [x] Cursores: `CursorDecoder` propio (CUR/ICO clásico: 1/4/8/24/32 bpp +
      máscara AND) → `SDL_CreateColorCursor`, respetando `bWindowCursor`;
      testeado contra los 7 `.cur` reales del repo. En runtime se cargan
      desde el directorio del juego (en Windows siguen embebidos como
      recursos).
* [x] `VK_*` puntuales: sin nuevos usos en el camino POSIX (el
      `_IsKeyDown(VK_MENU)` del WndProc es código Windows que se sustituye
      por el chequeo DIK_LMENU del main SDL).

**Aceptación (estado):** en Linux (driver dummy) el ejecutable
`KnightOnLine` crea la ventana 1024×768, procesa eventos, muestrea input y
cierra limpio (`--smoke 30`, ahora paso de CI); los tests
`WarFareClientTests` cubren mapeo de scancodes y decodificación de cursores
(5/5 suites verdes). La validación visual en macOS (ventana real, toggle
fullscreen, log de DIK al teclear) queda para la primera prueba en Mac —
la desconexión de sockets al cerrar vuelve con `CGameProcedure` (F5/F6).

### Fase 4 — Utilidades D3DX residuales y carga de texturas (esfuerzo: ~1 sp)

**Objetivo:** eliminar `d3dx9.h` de `My_3DStruct.h` y del código común, para
que la única dependencia gráfica restante sea la interfaz del device.

* [x] `My_3DStruct.h` ya no arrastra `<d3dx9.h>` fuera de Windows (hecho en
      F1 vía `Platform/D3D9Types.h`); el shim creció en F5 con los enums de
      estado (`D3DRS_/D3DTSS_/D3DSAMP_/D3DTS_`, blend/cull/cmp/fog/filtros) y
      `D3DMATRIX`, todos con los valores exactos de d3d9types.h.
* [x] **Decisión de alcance**: `D3DXLoadSurfaceFromSurface` (8),
      `D3DXCreateTextureFromFileEx` (1) y `D3DXGetErrorString` viven en
      `N3Texture`/`DFont`/`N3Eng`, que solo se compilan en Windows; su
      reemplazo (stb_image_resize + loaders propios) se hace cuando esos
      archivos se porten al RHI en F6/F7, quedando D3DX confinado al backend
      D3D9. La aceptación original ("d3dx solo en código Windows-only") ya
      se cumple.
* [x] `CWinCrypt` portable: `Platform/PlatformCrypto` (SHA-1 + RC4 propios,
      con vectores de test) reproduce la derivación
      `CryptDeriveKey(CALG_RC4, SHA1(cipher), 128 bits)`; Windows conserva
      CryptoAPI intacto. Reincorporado al subconjunto POSIX. Validación
      contra una textura cifrada real: pendiente de side-by-side en Windows.
* [x] Test de humo de assets (adaptado): `VMeshHeadless_test` escribe un
      `.n3vmesh` en el layout binario real, lo carga por la cadena del motor
      (`CN3VMesh` → `CN3BaseFileAccess` → `FileReader` con resolución
      case-insensitive) y lo renderiza por el RHI Null contando draw calls.
      El test con `.n3chr` reales del repo de assets llega cuando
      `N3Texture`/`N3Mesh` se porten (F6) — requieren creación de buffers.

**Aceptación (estado):** cumplida en su forma ajustada — `d3dx` solo aparece
en TUs que únicamente compila Windows; cripto de texturas portable con tests;
pipeline de carga de assets verificado headless en Linux (macOS vía CI).

### Fase 5 — RHI: interfaz de render abstracta (esfuerzo: ~3 sp)

**Objetivo:** que ningún archivo de juego hable con `LPDIRECT3DDEVICE9`.
Es el refactor más grande pero es *mecánico*: D3D9 fixed-function tiene una
superficie de API pequeña y repetitiva.

* [x] Inventario de usos de `CN3Base::s_lpD3DDev`: ~1.560 llamadas a ~40
      métodos (dominan SetRenderState 426, SetTextureStageState 337,
      GetRenderState 185, SetTexture 80, SetTransform 62, DrawPrimitiveUP 51).
* [x] `IRHIDevice` definido (`N3Base/RHI/RHIDevice.h`) calcando la
      superficie D3D9 1:1 (estados fixed-function, FVF, draws UP/indexados,
      luces/material/viewport); texturas y buffers siguen como punteros
      opacos hasta portar `N3Texture`/meshes con buffer-creation (F6).
      Migración = sustitución textual `s_lpD3DDev->` → `RHIDevice()->`.
* [x] **Clave de estado de pipeline** (`RHI/RHIStateKey.h`): hash FNV sobre
      los campos lógicos (FVF, primitiva, blend/z/alpha-test/fog/luces,
      ops de stages), con tests de igualdad/colisión y uso como clave de
      `unordered_map` — lista para la caché de pipelines de F6b.
* [x] Backend `RHIDeviceD3D9` (Windows): forwarder fino instalado por
      `CN3Eng` al crear el device y retirado en `Release()`. Backend
      `RHIDeviceNull` (portable): almacena estados (los `Get*` hacen
      round-trip), cuenta draws/presents y permite cargar assets sin GPU;
      instalado por el main SDL, que ya ejecuta la secuencia de frame
      Begin/Clear/End/Present por el RHI en cada tick (visible en el smoke).
* [ ] Migrar módulo a módulo: **primera tajada hecha** (~212 llamadas):
      `N3Base::RenderLines`, `CN3AlphaPrimitiveManager::Render` completo,
      `CN3VMesh` íntegro (primera clase de malla portable, compila y carga
      en POSIX) y los renderers de debug de `N3TransformCollision` (ya sin
      gates `_WIN32`). Restante (~1.350 llamadas, solo compila en Windows
      hoy): terreno, cielo, personajes/skins, efectos, UI, `N3Eng`,
      `DFont`, `N3Texture` y el código de juego WarFare — se migran por
      módulos en PRs pequeños con screenshot-diff en Windows, y cada módulo
      migrado se suma al subconjunto POSIX.

**Aceptación (parcial):** la abstracción está validada en ambos sentidos —
en Windows compila con el forwarder D3D9 instalado por `CN3Eng` (verifica
la CI de Windows; falta la validación visual side-by-side), y en POSIX la
tajada migrada corre de verdad: `N3Base.Tests` carga un `.n3vmesh` real por
el loader del motor y lo dibuja por el Null device. El hito "Windows 100%
sobre RHI" se alcanza al completar la migración por módulos.

### Fase 6 — Backend OpenGL (esfuerzo: ~4 sp, la fase más técnica)

**Objetivo:** implementar `RHIDeviceGL` (GL 3.3 core; en macOS contexto 4.1
core vía SDL) y ver el juego dibujar en macOS/Linux.

* [ ] Contexto GL por SDL (`SDL_GL_CreateContext`), loader `glad`, vsync por
      `SDL_GL_SetSwapInterval` (opción `VSyncEnabled` existente).
* [ ] Recursos: VBO/IBO (con soporte del patrón `DrawPrimitiveUP` vía buffer
      transitorio en ring), texturas (incl. DXT1/3/5 con
      `GL_EXT_texture_compression_s3tc`, disponible en macOS y Mesa; fallback
      a descompresión por CPU si faltara), render-to-texture si algún efecto
      lo usa.
* [ ] **Emulación de fixed-function con un über-shader**: matrices
      world/view/proj, hasta 2–3 texture stages con sus ops
      (MODULATE/ADD/SELECTARG…), iluminación por vértice (luces
      direccionales/puntuales como el juego usa en `LightMgr`), fog lineal,
      alpha test y vertex color. Los estados D3D9 del RHI se compilan a
      claves de pipeline + uniforms.
* [ ] Diferencias de convención: profundidad [0,1] vs [-1,1]
      (`glClipControl` no existe en GL 4.1 de macOS → ajustar matriz de
      proyección), origen de texturas (flip V al cargar o en sampler),
      semántica de `D3DCOLOR` BGRA en vertex color (usar
      `GL_BGRA`+normalized o swizzle al construir).
* [ ] Herramientas de validación: capturas RenderDoc **en Linux** (mismo
      backend GL; en macOS no existe depurador para GL) comparadas contra
      capturas D3D9 de referencia.

**Aceptación (hito visible):** escena de login (`GameProcLogIn`) y selección
de personaje renderizan correctamente en macOS; después mundo/terreno/agua/
efectos. Checklist de paridad visual por escena.

### Fase 6b — Backend SDL_GPU: el definitivo para macOS/Linux (esfuerzo: ~2 sp)

**Objetivo:** implementar `RHIDeviceSDLGPU` sobre la API SDL_GPU de SDL3
(Metal en macOS, Vulkan en Linux), que pasa a ser el backend por defecto;
GL queda como fallback/entorno de depuración en Linux.

* [ ] Caché de pipeline-objects usando la clave de estado definida en la RHI
      (fase 5); buffers transitorios para el patrón `DrawPrimitiveUP` con las
      transfer/copy passes de SDL_GPU.
* [ ] Portar el über-shader a HLSL y compilarlo offline con `SDL_shadercross`
      a SPIR-V + MSL (paso de build en CMake); las variantes del
      fixed-function se generan por permutación de defines.
* [ ] Paridad visual GL vs SDL_GPU con las mismas capturas de referencia;
      en macOS ahora sí con Xcode GPU Frame Capture.
* [ ] Selección de backend en runtime/`Option.ini` (`Renderer=GL|SDLGPU`)
      para poder bisectar bugs de render entre backends.

**Nota:** esta fase es paralelizable con F7 una vez validada la RHI (fase 5)
y el primer render GL (fase 6); no bloquea el hito "jugable".

**Aceptación:** el cliente corre en macOS sobre Metal (verificable con Metal
HUD / Xcode capture) con paridad visual respecto al backend GL y a D3D9.

### Fase 7 — Texto: fuentes y entrada/IME (esfuerzo: ~2 sp)

**Objetivo:** `DFont` y `N3UIEdit` sin GDI/IMM32.

* [ ] `DFont` sobre FreeType: rasterizar glifos a un atlas (misma interfaz
      `DrawText`/`SetText`); cachear por (fuente, tamaño, negrita/cursiva);
      cubrir Hangul + Latin-1 (los assets traen los nombres de fuente de
      Windows — mapear "굴림/Gulim" → fuente empaquetada con licencia libre,
      p. ej. Noto Sans KR, distribuida junto al cliente POSIX).
* [ ] `N3UIEdit` sin ventana `EDIT`: buffer de texto propio + caret ya
      existente (`CN3Caret`), alimentado por `SDL_EVENT_TEXT_INPUT` /
      `SDL_EVENT_TEXT_EDITING` (composición IME nativa de macOS y
      ibus/fcitx en Linux), `SDL_StartTextInput`/`SDL_SetTextInputArea` al
      enfocar/desenfocar; eliminar el paso `WM_COMMAND`/`EN_CHANGE` del main.
* [ ] Conversión UTF-8 ↔ CP949 en la frontera de red para chat/nombres
      (protocolo intacto).

**Aceptación:** login con usuario/contraseña escritos por teclado, chat
in-game con texto coreano y español (tildes) en macOS y Linux.

### Fase 8 — Recursos, empaquetado e integración de plataforma (esfuerzo: ~1 sp)

* [ ] Sustituir `Resource.rc`: iconos y cursores se cargan de archivos en
      `assets/` (los `.cur`/`.ico` actuales se convierten en build o runtime);
      el acelerador de debug (`IDR_MAIN_ACCELATOR`) se traduce a atajos SDL.
* [ ] macOS: target `MACOSX_BUNDLE` con `Info.plist` (nombre, `NSHighResolutionCapable`,
      icono `.icns` generado desde `WarFare.ico`), nota sobre firma ad-hoc
      (`codesign --force --deep -s -`) y Gatekeeper para builds locales.
* [ ] Linux: archivo `.desktop` + icono; rpath para deps vendorizadas.
* [ ] Rutas de escritura (logs, `Option.ini`) según convención de cada SO
      (definido en Fase 1).

**Aceptación:** `WarFare.app` arranca con doble clic en macOS; el binario
Linux corre desde un directorio de instalación limpio.

### Fase 9 — Estabilización y paridad (esfuerzo: ~2 sp, continuo)

* [ ] Pasadas de ASan/UBSan en macOS/Linux sobre el flujo login→mundo (el
      código legado tiene aritmética de punteros abundante; se esperan
      hallazgos también valiosos para Windows).
* [ ] Auditoría de supuestos de 32 bits / orden de bytes en (de)serialización
      de paquetes y formatos (`#pragma pack`, casts de punteros a `DWORD`).
      Ambas plataformas objetivo son little-endian de 64 bits, igual que el
      build x64 de Windows, así que el riesgo real es bajo pero hay que
      verificarlo con los tests de formato de la Fase 1.
* [ ] `-Wall -Wextra -Werror` limpio en clang (el proyecto ya lo activa por
      `OPENKO_COMPILE_WARNINGS_AS_ERROR`).
* [ ] Rendimiento: objetivo ≥ paridad con Windows/D3D9 en la misma máquina;
      perfilar con Instruments/`perf`.
* [ ] CI: jobs de macOS/Linux compilan el cliente completo y ejecutan los
      tests de humo (carga de assets + conexión a servidor local).
* [ ] Actualizar README y wiki (setup macOS/Linux del cliente).

---

## 4. Orden, dependencias e hitos

```
F0 Build/CI ─► F1 Fundaciones ─► F2 Red (Asio) ──────────────┐
                    │                                         │
                    ├─► F3 SDL ventana+input ─► F6 Backend GL ├─► F7 Texto/IME ─► F8 Empaquetado ─► F9 Estabilización
                    │                              ▲          │
                    └─► F4 Residuos D3DX ─► F5 RHI ┘          └─► F6b Backend SDL_GPU (paralelo a F7)
                                                              
   Hito A: "headless" (F1+F2): assets cargan y el socket habla con Ebenezer en macOS
   Hito B: "ventana viva" (F3): ventana + input en macOS
   Hito C: "login visible" (F5+F6 parcial): escena de login renderiza en macOS
   Hito D: "jugable" (F6+F7): entrar al mundo, moverse, chatear
   Hito E: "distribuible" (F8+F9): .app firmada ad-hoc + binario Linux + CI verde
           (con F6b integrada: macOS corre sobre Metal, no sobre GL deprecado)
```

F2, F3 y F4/F5 son paralelizables entre sí tras F1 si hay más de una persona;
F6b es paralelizable con F7. **Esfuerzo total estimado: ~21–22
semanas-persona**, dominado por F6 (GL) y F5 (RHI).

## 5. Riesgos y mitigaciones

| Riesgo | Impacto | Mitigación |
|---|---|---|
| Fidelidad de la emulación fixed-function (blending/fog/lighting sutilmente distintos) | Visual, medio | Capturas de referencia D3D9 por escena; comparar con RenderDoc (Linux)/Xcode capture (SDL_GPU); validar primero en Windows-GL si se desea (el backend GL también compila en Windows) |
| OpenGL deprecado y **sin depurador gráfico** en macOS | Estratégico + productividad, medio | GL es solo el backend de bring-up: se depura en Linux con RenderDoc (mismo código) y el backend definitivo de macOS es SDL_GPU/Metal (F6b), con Xcode GPU capture funcional |
| Apple retira OpenGL en una versión futura de macOS | Estratégico, bajo | F6b (SDL_GPU/Metal) elimina la dependencia de GL en macOS antes de distribución (hito E) |
| Assets con nombres en mayúsculas/minúsculas inconsistentes | Runtime Linux, alto y silencioso | Resolvedor case-insensitive con caché (F1) + verificación en CI con el fetch de assets (`OPENKO_FETCH_CLIENT_ASSETS`) |
| CP949 en strings/protocolo | Corrupción de texto | Conversión centralizada iconv en fronteras; tests con strings coreanos reales |
| Código legado con UB que MSVC tolera (aliasing, uninit) | Crashes en clang | ASan/UBSan desde la Fase 1; arreglos upstream benefician a Windows |
| Divergencia con upstream Open-KO durante el port | Merge pain | PRs pequeños por fase; los refactors neutros (PAL, Asio, RHI-sobre-D3D9) son aceptables upstream porque no cambian comportamiento |
| IME coreano en Linux (ibus/fcitx) | UX, medio | SDL3 lo abstrae; probar con `ibus-hangul` en CI manual |

## 6. Evaluación a futuro: ¿es viable un port a Rust?

**Veredicto: sí es viable, pero como *reescritura por etapas posterior* al port
POSIX en C++, no como sustituto de este plan.** Y con un matiz importante:
este plan, ejecutado tal cual, es la mejor preparación posible para esa
reescritura — las costuras que introduce (PAL, RHI, red desacoplada, UTF-8
interno) son exactamente las fronteras de crates que un port a Rust necesita.

### 6.1 Datos que condicionan la estrategia

* El cliente + motor suman **~120.000 líneas de C++** (`WarFare`, `N3Base`,
  `MathUtils`, `FileIO`), con **~68 clases en jerarquías de herencia** solo en
  `N3Base` (el patrón `CN3Base` → `CN3BaseFileAccess` → toda la familia
  `CN3*`), globals estáticos (`s_lpD3DDev`, `s_Options`) y propiedad de
  memoria por punteros crudos. Ese estilo OO profundo es justo lo que peor
  cruza una frontera FFI C++↔Rust.
* Consecuencia: la vía "oxidación incremental" (ir reemplazando clases una a
  una vía `cxx`/`autocxx` dentro del mismo binario) **no es práctica para el
  núcleo del motor**. Sí lo es para módulos hoja (ver 6.3).
* Los formatos binarios (assets `.n3*`, protocolo de red) se leen hoy con
  `memcpy` sobre structs empaquetados; en Rust se modelarían con
  (de)serialización explícita (`binrw`/`zerocopy`), lo que de paso elimina
  una clase entera de UB.

### 6.2 ¿Sirven las mismas herramientas? Equivalencias

La respuesta corta: **conceptualmente sí, literalmente solo en parte.** El
ecosistema Rust tiene sustitutos de primera clase, casi todos Rust puro (sin
FFI que mantener):

| Capa | Elegido para el port C++ | Óptimo en Rust | Notas |
|---|---|---|---|
| Ventana/input/eventos | SDL3 | **`winit`** (idiomático) o bindings `sdl3` | `winit` es el estándar del gamedev Rust y tiene IME; los bindings `sdl3` son válidos si se quiere minimizar divergencia con el port C++ |
| Gráficos | RHI propia + GL → SDL_GPU | **`wgpu`** | Es el análogo directo de SDL_GPU (API explícita con pipelines, backends Metal/Vulkan/DX12/GL) pero *nativo de Rust* y estándar del ecosistema. La caché de pipelines y el über-shader de F5/F6b se traducen 1:1; los shaders HLSL de SDL_shadercross se portan a WGSL con `naga` |
| Fuentes/texto | FreeType | **`cosmic-text`** (shaping+layout, cubre coreano) o `ab_glyph`/`fontdue` | Rust puro, sin FreeType que compilar |
| Audio | OpenAL Soft + mpg123 | **`kira`** (audio de juego) + **`symphonia`** (decodifica MP3 en Rust puro) | Reemplaza ambas dependencias C |
| Red | Asio (polling por frame) | `std::net` no bloqueante o **`mio`**; `tokio` solo si se quisiera async completo | El modelo "poll por tick" del plan se conserva idéntico |
| Matemática | `MathUtils` propio | **`glam`** (SIMD, estándar de facto) | `__Vector3`/`__Matrix44` mapean directo |
| Encoding CP949 | iconv | **`encoding_rs`** (EUC-KR/windows-949) | Rust puro |
| Formatos binarios | structs + `memcpy` | **`binrw`** / `zerocopy` | Gana validación y seguridad |
| Build | CMake + deps vendorizadas | **Cargo** | Simplificación enorme del apartado de dependencias |

Lectura clave: **la decisión de SDL_GPU (F6b) es una inversión que transfiere
a Rust.** SDL_GPU y `wgpu` comparten el mismo modelo mental (pipelines
explícitos, render/copy passes, shaders offline); el equipo que haya escrito
`RHIDeviceSDLGPU` puede escribir el backend `wgpu` casi de memoria. Si
hubiéramos apostado todo a OpenGL, ese conocimiento no transferiría (en Rust
el camino GL es ciudadano de segunda).

### 6.3 Estrategia recomendada si se decide ir a Rust

1. **No antes del hito D ("jugable") del port C++.** Reescribir en Rust un
   motor que aún depende de Win32 duplicaría el riesgo: se estaría portando
   plataforma y lenguaje a la vez, sin referencia ejecutable en la máquina de
   desarrollo.
2. **Pilotos en módulos hoja** (independientes del árbol OO, con tests
   contra los mismos assets): un crate `ko-formats` (parsers de `.n3chr`,
   `.n3shape`, `.tbl` con `binrw`, validado contra los tests de humo de F1),
   un crate `ko-protocol` (paquetes + `JvCryption`, validado contra
   Ebenezer), y opcionalmente herramientas de línea de comandos que hoy son
   Tools MFC. Estos crates son útiles por sí mismos (tooling, servidor) aunque
   la reescritura total no ocurra.
3. **Reescritura del cliente por capas, de fuera hacia dentro**, usando el
   cliente C++ POSIX como oráculo side-by-side (misma filosofía que el
   proyecto ya usa contra el cliente oficial): shell de ventana/render
   (`winit`+`wgpu`) → carga de mundo → gameplay. La frontera FFI con `cxx`
   solo como puente *transitorio* si se quiere hibridar, no como estado final.
4. Riesgos propios de Rust a vigilar: el gameplay legado está lleno de
   aliasing mutable (punteros cruzados entre managers) que el borrow checker
   no aceptará tal cual — la reescritura obliga a rediseñar propiedad de
   objetos (típicamente con handles/índices en lugar de punteros, estilo ECS
   o arenas), lo que es deseable pero no es "traducción", es rediseño; y el
   costo es comparable al de este plan completo (no es menor que F0–F9).

## 7. Alternativa pragmática (fuera del alcance de este plan)

Para *ejecutar* el cliente hoy en macOS/Linux sin portarlo: **Wine/CrossOver**
(con DXVK traduciendo D3D9→Vulkan en Linux, y D3DMetal/MoltenVK vía Game
Porting Toolkit o CrossOver en macOS). Útil como referencia de comportamiento
("ground truth") durante el port, pero no sustituye el objetivo de build
nativo de esta rama.

## 8. Referencias del código citadas por este plan

* Gate de plataforma: `CMakeLists.txt:49-63`
* Punto de entrada Win32: `src/Client/WarFare/WarFareMain.cpp`
* PCH con winsock: `src/Client/WarFare/StdAfx.h`
* DirectInput: `src/Client/WarFare/LocalInput.{h,cpp}`
* Red asíncrona por mensajes: `src/Client/WarFare/APISocket.cpp:150` (`WSAAsyncSelect`)
* Device D3D9 global: `src/N3Base/N3Base.h:155`, creación en `src/N3Base/N3Eng.cpp:24`
* Fuentes GDI: `src/N3Base/DFont.cpp`
* Edit nativo + IME: `src/N3Base/N3UIEdit.cpp:106,599-611`
* Header base con d3dx9: `src/N3Base/My_3DStruct.h:6`
* Matemática ya portable: `src/MathUtils/`
* Audio ya portable: `src/N3Base/Audio*`, `N3Snd*`; deps `openal-soft`, `mpg123`
* Asio vendorizado: `deps/asio`
