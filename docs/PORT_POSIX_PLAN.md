# Plan de implementación: port del cliente WarFare a plataformas POSIX (macOS / Linux)

> **Estado:** en ejecución — Hito D ("jugable") alcanzado y validado en un
> Mac real contra un servidor Ebenezer local (fases 0-8 completadas; F9 en
> curso, ver checklist)
> **Rama:** `feature/port-posix`
> **Para agentes nuevos:** leer primero `docs/PORT_POSIX_CONTEXT.md` (reglas,
> gotchas, build/test). Las fases 6+ están subdivididas en tareas `T*`
> independientes, pensadas para ejecutarse en sesiones separadas.
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
* [x] Migrar módulo a módulo — **completada en alcance cliente** (el conteo
      intermedio "73%, ~420 llamadas restantes" quedó obsoleto y no se
      actualizó tras la migración masiva; corregido en la auditoría de
      cierre). Migración masiva clasificada por archivo: 45+ archivos de
      N3Base + WarFare, incluyendo la familia de mallas completa (que
      resultó dibujar solo con draws UP desde memoria de sistema — sin
      buffers GPU), personajes/skins/joints, cámara/luces/escena/shapes,
      cielo completo, clima, terreno, agua y todos los FX y UIs.
      *(Cierre (auditoría fresca de `s_lpD3DDev`): el build POSIX de
      `N3Base_client` y `WarFare.Core` incluye hoy TODOS los grupos de
      fuentes (escenas, mundo, terreno, clima, FX, la UI completa) — la
      única exclusión es `BitMapFile.cpp`, cuyo único consumidor cliente
      es la captura de pantalla GDI de `GameProcedure` (gated `_WIN32`).
      Los usos directos de D3D9 que quedan son por diseño, no deuda:
      (a) `N3Eng.cpp` — creación/reset del device en Windows: ES el
      backend D3D9; (b) `N3EngTool.cpp`, `Pick.cpp` y los bloques
      `_N3TOOL` de `N3Texture` — exclusivos del target `N3Base_tool` de
      las herramientas de desarrollo MSVC, fuera del alcance del port;
      (c) `DFont.h`/`GetRawD3D()` bajo `_WIN32` — rama GDI. Las demás
      referencias solo pasan el puntero (null en POSIX) a
      `InitDeviceObjects`, que lo ignora en la rama FreeType, o son
      código comentado. Verificado en runtime: test-scene GL 60 frames +
      dump de frame OK, y el juego completo corre in-game en macOS
      (Hito D).)*

**Aceptación:** la abstracción está validada en ambos sentidos — en
Windows todo el código de render llama por el forwarder D3D9 que instala
`CN3Eng` (100% sobre RHI, salvo el propio bootstrap del device y las
herramientas `_N3TOOL`), y en POSIX el juego completo corre de verdad
sobre `RHIDeviceGL`/`RHIDeviceNull` (Hito D alcanzado in-game). Pendiente
solo la validación visual side-by-side Windows, cubierta por F9.

### Fase 6 — Recursos RHI + Backend OpenGL (esfuerzo: ~4-5 sp, subdividida)

> Cada tarea es autocontenida, con su criterio de aceptación, y deja la rama
> verde (build + 6 suites de tests + smoke). Orden: T6.1 → T6.2 → (T6.3‖T6.4)
> → T6.5 → T6.6 → T6.7 → T6.8. Ver `PORT_POSIX_CONTEXT.md` §4-5 antes de
> empezar cualquiera.

* [x] **T6.1 — Buffers RHI.** Añadir `IRHIVertexBuffer`/`IRHIIndexBuffer`
      (Lock/Unlock/Release, semántica IDirect3DVertexBuffer9) +
      `CreateVertexBuffer`/`CreateIndexBuffer` en `IRHIDevice`; cambiar las
      firmas de `SetStreamSource`/`SetIndices` a los tipos RHI. Impl Null:
      memoria de sistema real (malloc; Lock devuelve el puntero). Impl D3D9:
      wrapper. Migrar los 4 usuarios de `CreateVertexBuffer`
      (`N3TerrainPatch`, `N3GERain`, `N3GESnow`, `N3UIImage`) y el cast del
      alpha-manager. Tests en `tests/N3Base` (round-trip Lock/escritura).
      *Aceptación:* los 4 archivos compilan en POSIX y entran al subset;
      Windows CI verde.
* [x] **T6.2 — Texturas RHI + port de N3Texture.** `IRHITexture`
      (LockRect/UnlockRect/GetLevelDesc por nivel) + `CreateTexture`;
      `SetTexture` acepta el tipo RHI. Impl Null: almacenamiento por nivel
      con tamaño correcto (¡bloques 4x4 para DXT!). Migrar `N3Texture.cpp`
      (miembro `m_lpTexture` → `IRHITexture*`); los caminos con D3DX
      (`D3DXLoadSurfaceFromSurface`, save, LOD-rescale) quedan `#ifdef _WIN32`
      por ahora. Test: generar un `.dxt` sintético mínimo (formato del juego)
      y cargarlo headless; si `OPENKO_FETCH_CLIENT_ASSETS=ON`, smoke con una
      textura real. *Aceptación:* `N3Texture` en el subset POSIX; carga DXT
      headless testeada.
* [x] **T6.3 — Rezagados de device (mecánica).** `ValidateDevice` y
      `SetScissorRect` entran a `IRHIDevice` (Null: no-op OK; D3D9: forward);
      migrar `N3Terrain.cpp` y `UIHotKeyDlg.cpp`. `N3Cloak`
      (`SetVertexShader(nullptr)` x2) se migra gateando esas 2 líneas.
      *Aceptación:* `grep s_lpD3DDev src/Client/WarFare src/N3Base` solo
      devuelve `N3Eng*`, `DFont`, `N3Texture`(win-paths) y `N3UIEdit`.
* [x] **T6.4 — Recursos de texto portables.** `text_resources.h`/`resource.h`
      usan `LoadString` del `.rc` en Windows. Extraer las cadenas IDS_* a una
      tabla C++ o archivo de datos cargado en runtime, manteniendo el camino
      `.rc` en Windows (`fmt::format_text_resource` como API común).
      *Aceptación:* un TU de WarFare que use `IDS_*` compila en POSIX.
      *(Hecho: upstream ya movió `format_text_resource` a la tabla TBL
      `Data\Texts_*.tbl` — no quedaba `LoadString`. El único acople era que
      la definición del static `CGameBase::s_pTbl_Texts` vivía en el pesado
      `GameBase.cpp`; se movió a `ClientResourceFormatter.cpp` y este entró al
      subset POSIX. Test end-to-end en `tests/WarFare` que cifra un `.tbl`
      sintético, lo carga y formatea `IDS_*` con `%d`/`%s`.)*
* [x] **T6.5 — Backend GL a: contexto + clear.** `RHIDeviceGL` mínimo:
      contexto vía `SDL_GL_CreateContext` + loader `glad` (vendorizar
      generado GL 3.3/4.1 core en `deps/` o `src/N3Base/RHI/glad/`),
      `Clear`/`BeginScene`/`EndScene`/`Present` (SwapWindow), vsync según
      `bVSyncEnabled`. Selección de backend por `Option.ini`
      (`Renderer=Null|GL`). *Aceptación:* la ventana deja de ser negra — se
      ve el color de clear (probar en Mac); smoke CI sigue en dummy+Null.
      *(Hecho: `RHIDeviceGL` hereda de `RHIDeviceNull` y solo sobrescribe
      `Clear`/`Present`; contexto GL 3.3 core (forward-compat, para macOS),
      vsync por `SDL_GL_SetSwapInterval`. En vez de vendorizar glad generado
      se usa un loader minimal propio (`GLLoader.h/.cpp`, `namespace gl`) que
      resuelve solo las funciones usadas vía `SDL_GL_GetProcAddress` — sin
      dep de link a GL, crece en T6.6/T6.7. Selección por `[Screen]
      Renderer=GL` o flag `--renderer gl`; default Null. Degrada a Null si el
      driver no tiene GL (p.ej. dummy de CI). **Falta validación visual en un
      Mac** — no hay GL real en el contenedor de build.)*
* [x] **T6.6 — Backend GL b: geometría + texturas.** VBO/IBO desde los
      buffers RHI, ring-buffer transitorio para `Draw*UP`, texturas
      (DXT vía `GL_EXT_texture_compression_s3tc`, RGBA/565 sin comprimir),
      conversión BGRA de `D3DCOLOR` en vertex colors, flip V.
      *Aceptación:* test visual con la malla del smoke (quad) dibujada.
      *(Hecho: `RHIVertexBufferGL`/`RHIIndexBufferGL`/`RHITextureGL` extienden
      los Null — la copia en RAM sigue siendo la fuente de verdad y el objeto
      GL se (re)sube perezosamente al bindear tras cada Unlock. Buffer de
      streaming para `Draw*UP`. Texturas: DXT1/3/5 comprimidas, BGRA 8888,
      1555/4444 REV, RGB; `TEXTURE_MAX_LEVEL` para cadenas que paran en 4x4;
      caps S3TC/max-size publicados a `s_dwTextureCaps`/`s_DevCaps` como hace
      CN3Eng. Vertex colors BGRA por `ARB_vertex_array_bgra`. El flip V
      resultó innecesario para sampling (mismo orden de filas que D3D).
      **Validado con píxeles reales** en Linux headless (Xvfb + Mesa
      llvmpipe): quad texturizado dibujado y verificado por lectura de
      framebuffer nativa (`--dump-frame`); la escena está en `--test-scene`.)*
* [x] **T6.7 — Backend GL c: über-shader fixed-function v1.** Matrices
      world/view/proj, 2 texture stages (MODULATE/SELECTARG1/ADD/DISABLE),
      alpha blend/test, fog lineal, iluminación por vértice
      (direccional+punto), materiales. Estados D3D del RHI → uniforms/estado
      GL. Ajustar proyección para depth [-1,1].
      *Aceptación:* con assets reales en un Mac, `GameProcLogIn` (T6.8)
      o en su defecto una escena de prueba con `.n3shape` real se ve
      correcta; comparar contra captura D3D9 de referencia.
      *(Hecho: über-shader GLSL 330 único — WVP con remap de depth
      [0,1]→[-1,1] en clip space, path XYZRHW pantalla-espacio, 3 stages
      (terreno usa 0..2) con DISABLE/SELECTARG/MODULATE(2X/4X)/ADD + TFACTOR,
      alpha test por discard, fog lineal en depth de vista, iluminación
      por vértice direccional+puntual con material/ambient global, culling
      con FrontFace(CW) por la diferencia de winding D3D/GL. Estados leídos
      de los mapas heredados del Null en cada draw (correcto primero,
      optimizable después). Verificado en Linux con la escena sintética
      (esquinas/centro exactos por readback nativo). **Pendiente para
      T6.8/Mac:** validación con `.n3shape`/assets reales contra captura
      D3D9 de referencia.)*
* [x] **T6.8 — Conectar CGameProcedure (hito C).** Compilar en POSIX
      `GameBase`/`GameEng`/`GameProcedure` + escena de login + los `N3UI*` de
      N3Base con el patrón de gates ya establecido; sustituir el pase de
      diagnóstico del main SDL por `StaticMemberInit` +
      `TickActive`/`RenderActive` + desconexión de sockets al salir. `DFont`
      provisional: stub que no dibuja texto (el real llega en F7).
      *Aceptación:* **pantalla de login renderizando en macOS** (sin texto es
      aceptable para el hito).
      *(Hecho. **TODO el cliente compila y enlaza en POSIX** y la escena de
      login corre de extremo a extremo. Incrementos (12 commits en*
      *`feature/port-posix`, todos verdes en Linux — build `-Werror`, ctest 6/6,*
      *smoke):*
      *1) stub POSIX de `DFont` (no dibuja texto) + shims GDI, con test*
      *`DFontStub_test` que fija el contrato; 2) framework `N3UI*` de N3Base;*
      *3) `CN3Eng`: device/adaptador D3D9 bajo `#ifdef _WIN32`, estado de render*
      *por `RHIDevice()` (idéntico en Windows vía el forwarder D3D9); 4)*
      *`CGameEng`/`CGameBase`; 5) jugadores; 6) UI base + `GameCursor`; 7)*
      *`CGameProcedure` con `StaticMemberInit` reducido a solo el proc de login*
      *en POSIX + `GameProcLogIn`/`UILogin`; 8) fix de propiedad del device RHI*
      *(en POSIX lo posee el entry point SDL, así que `~CN3Eng`/`Release` ya no*
      *lo borran → evita doble free); 9) subsistema de mundo completo*
      *(terreno/agua/efectos/dungeon/luces/objetos + `CN3FXMgr`); 10) `N3UIEdit`*
      *(el caret dibuja; la entrada de texto del SO llega en T7.2); 11) escena*
      *in-game completa (`GameProcMain` ~8k líneas, ~40 diálogos de UI, procs de*
      *nación/personaje); 12) cableado del main SDL tras la bandera opcional*
      *`--scene login` (`StaticMemberInit`+`TickActive`/`RenderActive`+*
      *desconexión de sockets), dejando el camino de diagnóstico por defecto*
      *intacto para el smoke de CI. Shims nuevos en `Platform/`:*
      *`_splitpath`/`_makepath`/`_MAX_*`, `_findfirst/next/close`+`_finddata_t`*
      *(`PlatformFileFind.h`), `GetCurrentDirectory`, `Sleep`/`GetTickCount64`,*
      *`GetPrivateProfile*` (Server.Ini, `PlatformIni.h`),*
      *`lstrcpy/cat/cpyn/cmp/cmpiA`+`_strlwr`, `MB_*`, `PostQuitMessage`,*
      *`GlobalAlloc/Free`, `EqualRect`, `GetUserDefaultLangID`, `HCURSOR`,*
      *`byte`, `LPWORD`, `D3DBLENDOP`, `D3DRECT`, `D3DDISPLAYMODE`,*
      *`D3DERR_*`/`D3DPMISCCAPS_BLENDOP`. Verificado headless en Linux:*
      *`--scene login` recorre todo el bring-up (ventana, backend Null, carga de*
      *tablas que fallan con gracia sin assets, construcción de*
      *managers de mundo/jugador, init de sonido) y sale limpio en el chequeo de*
      *recursos de UI — sin crash. **Pendiente:** verificación visual del login*
      *en un Mac con los datos del juego + backend GL (entorno del usuario).)*

**Aceptación de fase:** hito C — login visible en macOS con backend GL;
CI verde en las 3 plataformas; el subset POSIX incluye terreno/UI base.

### Fase 6b — Backend SDL_GPU: el definitivo para macOS/Linux (esfuerzo: ~2 sp)

* [x] **T6b.0/T6b.1 — Toolchain + über-shader.** *(Hecho, con una desviación
      justificada del plan: la fuente es Vulkan GLSL en vez de HLSL — port
      literal del über-shader GL ya probado (`shaders/uber_sdlgpu.vert/.frag`),
      y el toolchain es `glslangValidator` (GLSL→SPIR-V) + `spirv-cross`
      (SPIR-V→MSL), ambos en los archives de apt/brew, en vez de arrastrar
      `SDL_shadercross`+DXC al build. `shaders/build_shaders.sh` regenera
      `ShaderBlobsSDLGPU.h` (COMMITEADO: el build normal no necesita ningún
      toolchain de shaders). El MSL generado cae exactamente en el modelo de
      bindings del backend Metal de SDL (buffer(0), texture/sampler(0..2),
      stage_in, entry main0). Diferencias respecto al shader GL, impuestas
      por las APIs: sin remap de profundidad (SDL_GPU mantiene z∈[0,1] como
      D3D), swizzle BGRA del color de vértice en el shader (SDL_GPU no tiene
      formato de vértice BGRA), uniforms en dos bloques std140 (sets 1 y 3),
      y gl_PointSize escrito para point lists.)*
* [x] **T6b.2 — `RHIDeviceSDLGPU`.** *(Hecho. Arquitectura "grabar y
      reproducir": D3D9 permite subir datos y dibujar en cualquier punto del
      frame, pero SDL_GPU prohíbe copy passes dentro de un render pass — los
      draws se GRABAN durante el frame (snapshot de estado/uniforms + copia
      de vértices/índices a una arena) y `Present()` ejecuta un copy pass
      con todos los uploads seguido del render pass que los reproduce.
      `Clear()` a mitad de frame = corte de pass con load ops. Render a
      target offscreen + blit al swapchain (habilita los hooks de test
      `ReadCenterPixel`/`DumpFramePPM`). Caché de pipelines por clave
      (FVF/stride/topología/blend/z/cull), caché de samplers, triangle fans
      expandidos a listas indexadas (SDL_GPU no tiene fans; la UI entera
      dibuja quads como fans), texturas BC1-3 passthrough y 16bpp/24bpp
      expandidos a BGRA8 en CPU, texturas liberadas a mitad de frame van a
      un graveyard drenado tras el submit. Los buffers del motor siguen en
      memoria de sistema (Null) y se streamean por frame — correcto primero,
      buffers GPU persistentes como optimización futura. 8 tests de los
      helpers puros (`SDLGPUTranslate_test.cpp`) + `--renderer sdlgpu` /
      `Renderer=SDLGPU` cableados.)*
* [~] **T6b.3 — Paridad y default.** *(Revertido a parcial tras el
      hallazgo in-world: sobre Metal, la carga de mundo (~2000 draws y ~60
      ciclos del ring de uniforms de 32KB de SDL por frame) muestra objetos
      sin textura y geometría corrupta, mientras que el login (25 draws)
      es correcto y Vulkan es correcto SIEMPRE — incluido un test de estrés
      de 2000 draws con uniforms/texturas por-draw verificado píxel a píxel
      (`ManyDrawsKeepPerDrawUniformsAndTexturesStraight`). Los diagnósticos
      in-world del usuario confirman que las texturas SÍ llegan a la GPU
      (0 fallbacks, contador de "quería textura y no tenía" ~ruido), así
      que el fallo está en el backend Metal de SDL a escala, no en nuestra
      capa. Mientras se investiga (Xcode GPU capture / bump de SDL), el
      default vuelve a GL en TODAS las plataformas y SDLGPU queda opt-in
      (`Renderer=SDLGPU` / `--renderer sdlgpu`). Lo ya validado se
      mantiene: paridad pixel-perfect del
      test-scene GL vs SDL_GPU verificada en Linux/Vulkan-lavapipe — diff
      máximo 1/255 por redondeo, 0 píxeles con diff>16 de 786k, mismo píxel
      central — y validación in-game sobre Metal en Mac real (Apple
      Silicon): el usuario confirmó el juego corriendo "con muy buena
      calidad" vía `--renderer sdlgpu`. Con eso, SDLGPU es el DEFAULT en
      macOS (GameOptions.cpp), con un probe en el arranque que cae a GL
      automáticamente si no hay driver Metal/Vulkan (el probe corre antes
      de crear la ventana porque el flag OpenGL debe fijarse en su
      creación). Linux mantiene GL de default — Vulkan aún no es universal
      ahí. 3 tests de integración GPU (`RHIDeviceSDLGPU_gpu_test.cpp`)
      cubren el camino exacto de la UI del juego — quads XYZRHW en fan con
      MODULATE sobre DXT1/BGRA8/mipmaps — contra un device real
      (Vulkan-lavapipe en CI; se saltan limpio sin GPU), y el backend
      registra diagnósticos dispersos por frame para cazar regresiones de
      binding/upload en el campo. Un reporte inicial de "todo blanco" en
      Metal no se reprodujo tras recompilar — build parcial del bundle;
      los diagnósticos quedan para detectarlo si reaparece.)*

**Nota:** paralelizable con F7 tras T6.7. **Aceptación:** cliente corriendo
sobre Metal en macOS con paridad visual respecto a GL y D3D9.

### Fase 7 — Texto: fuentes y entrada/IME (esfuerzo: ~2 sp)

**Objetivo:** `DFont` y `N3UIEdit` sin GDI/IMM32.

* [x] **T7.1 — `DFont` sobre FreeType.** Atlas de glifos a `IRHITexture`
      (misma API pública `DrawText`/`SetText`); cachear por (fuente, tamaño,
      estilo); cubrir Hangul + Latin-1; mapear "굴림/Gulim" → Noto Sans KR
      empaquetada. GDI queda `#ifdef _WIN32`. *Aceptación:* texto visible en
      la pantalla de login en macOS; test headless que rasteriza y verifica
      que el atlas tiene píxeles.
      *(Hecho: la sección POSIX de `DFont.cpp` reemplaza el stub de T6.8 con
      el mismo diseño que GDI — `SetText` rasteriza el string completo a una
      textura A4R4G4B4 por instancia (misma heurística de tamaño 32..2048 y
      empaquetado alpha-nibble) y genera un quad XYZRHW por tramo de fila;
      `DrawText` traslada/tiñe los quads cacheados y dibuja por
      `DrawPrimitiveUP` con el mismo footprint de render states que Windows
      (alpha blend over, sin Z/fog, stage 0 modulate). FT_Library/FT_Face
      compartidos y refcontados vía `s_iInstanceCount` (como el DC de GDI);
      la cara se re-dimensiona por operación a 96 DPI (= `MulDiv(h,96,72)`).
      Encoding: CP949 → Unicode en la frontera con `PlatformEncoding` +
      fallback Latin-1. Resolución de fuente: `<datos>/Fonts/*.ttf|otf|ttc`
      primero, luego fuentes de sistema con cobertura Hangul (AppleSDGothic
      en macOS, Noto CJK/Nanum en Linux) y fallbacks latinos (DejaVu). En vez
      de empaquetar Noto Sans KR, el directorio `Fonts/` permite usar la
      fuente exacta que se quiera. Negrita/cursiva sintéticas: TODO (el login
      no las usa). FreeType vía `FindFreetypeFont.cmake` (sistema primero,
      fallback FetchContent VER-2-13-3). Test `DFontFT_test` (reemplaza a
      `DFontStub_test`): rasteriza "Hello" headless por el RHI Null, verifica
      texels con alpha en el atlas, extents crecientes por prefijo (contrato
      del word-wrap), draw call real y liberación al setear "". **Pendiente:**
      validación visual en un Mac con los datos del juego.)*
* [x] **T7.2 — `N3UIEdit` sin ventana `EDIT`.** Buffer de texto propio +
      caret existente, alimentado por `SDL_EVENT_TEXT_INPUT`/`TEXT_EDITING`
      (IME nativo), `SDL_StartTextInput`/`SDL_SetTextInputArea` en
      focus/blur; eliminar el paso `WM_COMMAND` del camino POSIX.
      *Aceptación:* login con usuario/contraseña tecleados; composición
      coreana funcional en macOS.
      *(Hecho: `CN3UIEdit` gana en POSIX entry points estáticos —
      `OnTextInput` (texto confirmado), `OnTextEditing` (preedit del IME,
      con reemplazo in-place vía `m_iCompLength`) y `OnKeyDown` (DIK_*:
      backspace/delete/flechas/home/end/enter, DBCS-aware con el mismo
      walk de paridad que `IsHangulMiddleByte`; Tab NO se consume para que
      la circulación de foco siga el camino `OnKeyPress` como en Windows).
      Enter notifica `UIMSG_EDIT_RETURN` al padre igual que el
      `EditWndProc` Win32. La edición muta el buffer lógico por
      `SetString`, así el enmascarado de password y el posicionamiento del
      caret existentes siguen funcionando; UTF-8→CP949 en la frontera con
      `PlatformEncoding`. Focus/blur llaman hooks registrables
      (`SetTextInputHooks`) — el main SDL registra
      `SDL_SetTextInputArea`+`SDL_StartTextInput` / `SDL_StopTextInput`, y
      enruta `SDL_EVENT_TEXT_INPUT`/`TEXT_EDITING`/`KEY_DOWN` (via
      `SdlScancodeToDik`) al edit con foco. No había paso `WM_COMMAND` que
      eliminar (el WndProc es Windows-only). Test `UIEditTextInput_test`
      (6 casos): inserción/edición ASCII, edición Hangul por caracteres
      completos, composición IME (reemplazo/commit/cancelación), password
      enmascarado solo en display, clamp de longitud sin partir pares
      DBCS, Enter→padre + hooks de foco. **Pendiente:** validación de
      composición coreana real en un Mac (requiere IME del SO).)*
* [x] **T7.3 — Fronteras de encoding de chat.** `Cp949ToUtf8`/`Utf8ToCp949`
      en los puntos de entrada/salida de texto de red (chat, nombres).
      *Aceptación:* round-trip de chat con tildes y Hangul contra Ebenezer
      local.
      *(Hecho. Nuevo header `NetworkEncoding.h` con dos wrappers
      centralizados — `NetToLocal(s)` y `LocalToNet(s)` — que en Windows son
      pass-through (`const std::string&`, sin copia) y en POSIX son
      `Cp949ToUtf8`/`Utf8ToCp949` sobre `PlatformEncoding`. El buffer del
      edit (T7.2) cambia a **UTF-8 puro** en POSIX — `N3UIEdit` ahora usa
      un walker de bytes UTF-8 (`Utf8SeqLen`, salto sobre bytes de
      continuación `10xxxxxx`) para caret/backspace/delete/flechas, sin
      pasar por CP949 en cada tecla. `DFont::DecodeGameText` valida
      estructuralmente UTF-8 primero (cheap check) y solo cae a
      `Cp949ToUtf8` cuando el input no es UTF-8 válido, así el mismo font
      backend rinde texto de UI (UTF-8) y strings de assets (CP949 legacy)
      sin ambigüedad. Aplicado `NetToLocal` en TODAS las lecturas de
      strings del servidor (chat/whispers/notice/user-in/party/knights/warp-list/
      dropped-item/character-list/friends/trade-BBS/party-BBS/notice, ~30
      sitios en `GameProcMain`, `GameProcCharacterSelect`, `UIVarious`,
      `UIPartyBBS`, `UITradeSellBBS`, `UIKnightsOperation`,
      `GameProcLogIn_{1098,1298}`), y `LocalToNet` en TODOS los envíos con
      texto de usuario (chat, whispers, admin, knights create/leave/appoint,
      party target, account/password de login, character select/create/delete,
      friends list, trade BBS, kickout re-request en el multi-conexión).
      Tests: `NetworkEncoding_test` valida round-trip ASCII, Hangul (가/간
      via CP949 de 2 bytes), tildes latinas y strings mixtos; en Windows
      además verifica que los wrappers no copian el buffer (comparación de
      dirección). `UIEditTextInput_test` actualizado para las longitudes
      UTF-8 en POSIX (3 bytes/sílaba Hangul). Suite 6/6 verde. **Pendiente:**
      validación side-by-side contra Ebenezer local (queda como
      verificación manual del usuario).)*
**Aceptación:** login con usuario/contraseña escritos por teclado, chat
in-game con texto coreano y español (tildes) en macOS y Linux.

### Fase 8 — Recursos, empaquetado e integración de plataforma (esfuerzo: ~1 sp)

* [x] Sustituir `Resource.rc`: los `.cur` viven junto al binario en Linux y
      en `Contents/Resources` en el bundle de macOS. `Resource.rc` sigue
      siendo el camino de Windows sin cambios. El acelerador de debug
      (`IDR_MAIN_ACCELATOR`) se traduce a atajos SDL — pendiente porque
      solo aplica al modo debug del ingame, fuera del alcance actual.
      *(Hecho: nueva lista `WARFARE_CLIENT_RESOURCES` en el CMake de
      WarFare; los 7 `.cur` (Normal/Click/Attack + `1` variants + repair 0/1)
      se copian por `add_custom_command POST_BUILD` en Linux y se marcan
      con `MACOSX_PACKAGE_LOCATION Resources` en macOS. `SetupWindowCursor`
      ahora resuelve el path con `FindClientResource`: mira primero junto
      al binario (`GetExecutableDir`), luego `Contents/Resources/` en el
      bundle, y por último el directorio del juego como fallback.)*
* [x] macOS: target `MACOSX_BUNDLE` con `Info.plist` (nombre,
      `NSHighResolutionCapable`, `LSApplicationCategoryType`,
      `LSMinimumSystemVersion`), icono `.icns` generado desde `WarFare.ico`,
      nota sobre firma ad-hoc (`codesign --force --deep -s -`) y Gatekeeper
      para builds locales.
      *(Hecho parcial: `MACOSX_BUNDLE TRUE` + `Info.plist.in`
      configurable con nombre "Knight OnLine", bundle id
      `org.openko.KnightOnLine`, `NSHighResolutionCapable`, categoría
      role-playing y `LSMinimumSystemVersion 11.0`. Los 7 `.cur` se stagean
      dentro de `Contents/Resources/`. La generación del `.icns` desde el
      `WarFare.ico` requiere `iconutil` de macOS y queda como paso manual
      (no bloqueante — sin `.icns` el sistema muestra un icono genérico);
      si `KnightOnLine.icns` aparece en el árbol, CMake lo mete en el
      bundle automáticamente. Firma ad-hoc + Gatekeeper: documentar en el
      README cuando el usuario cierre el ciclo en su Mac.)*
* [x] Linux: archivo `.desktop` + icono; rpath para deps vendorizadas.
      *(Hecho: `openko-client.desktop.in` configurado con `Exec` apuntando
      a `${CMAKE_INSTALL_PREFIX}/bin/KnightOnLine` y categorías
      `Game;RolePlaying;`. `INSTALL_RPATH "$ORIGIN"` en el target — con
      `BUILD_WITH_INSTALL_RPATH` explícitamente NO puesto para que las
      dev builds sigan usando el rpath del árbol de build; solo el binario
      instalado obtiene `$ORIGIN`. Regla `install`: binario en
      `${prefix}/bin/`, cursores hermanos en `${prefix}/bin/`, `.desktop`
      en `${prefix}/share/applications/`. Icono `openko-client.png`
      referenciado por el `.desktop` queda como decoración opcional.)*
* [x] Rutas de escritura (logs, `Option.ini`) según convención de cada SO
      (definido en Fase 1).
      *(Hecho: `Log.txt` ahora abre bajo `GetUserConfigDir()` en POSIX
      (`~/Library/Application Support/OpenKO/` en macOS,
      `$XDG_CONFIG_HOME/openko` o `~/.config/openko/` en Linux) — el
      directorio se crea si no existe. Windows conserva el comportamiento
      histórico "junto al ejecutable". `Option.ini` ya se resolvía por
      `Platform/PlatformPaths` en F3.)*
* [x] Rutas de LECTURA del cliente (Server.Ini, Data/, UI/): flag `--data
      <path>` + env var `OPENKO_GAME_DATA` + auto-discovery + staging
      automático de `assets/Client/` a `GameData/` en cada build.
      *(Hecho: el CWD dejó de ser confiable cuando el binario vive dentro
      del `.app` bundle (double-click o run desde IDE no ponen el CWD en
      los datos del juego). El build system ahora replica `<repo>/assets/
      Client/` en `<exe-dir>/GameData/` (Linux) o en `Contents/Resources/
      GameData/` (bundle macOS) vía `copy_directory_if_different` en
      POST_BUILD — el runtime queda auto-contenido. Nueva resolución en
      el main SDL, tras `LoadGameOptions`: precedencia `--data <path>` →
      env `OPENKO_GAME_DATA` → auto-discovery contra una lista de
      candidatos (CWD, `<exe-dir>/GameData/`, exe dir, `Contents/
      Resources/GameData/`, `Contents/Resources/`, padre del bundle,
      `~/GameData`, `~/Library/Application Support/OpenKO/GameData` en
      macOS o `~/.local/share/openko/GameData` en Linux). El primero que
      contiene `Data/` o `Server.Ini` gana. Log claro: `game data
      directory: <path>` si acierta, o error accionable si no.
      Adicionalmente, **login scene ahora es el default** en el main SDL
      (el usuario esperaba el menú de presentación por defecto): las
      flags `--smoke <N>` / `--test-scene` / `--diagnostics` optan por
      el path de diagnóstico. `--scene login` queda como no-op de
      compat. CI sigue verde (usa `--smoke 30`).)*

**Aceptación:** `WarFare.app` arranca con doble clic en macOS; el binario
Linux corre desde un directorio de instalación limpio.
*(Estado: `cmake --install` produce un layout válido — binario + cursores
en `bin/`, `.desktop` en `share/applications/`, rpath `$ORIGIN` para
resolver deps vendorizadas hermanas. Verificado en Linux vía
`DESTDIR=/tmp/... cmake --install`. macOS bundle listo estructuralmente;
la validación end-to-end en un Mac real queda como paso del usuario.)*

### Fase 9 — Estabilización y paridad (esfuerzo: ~2 sp, continuo)

* [~] Pasadas de ASan/UBSan en macOS/Linux sobre el flujo login→mundo (el
      código legado tiene aritmética de punteros abundante; se esperan
      hallazgos también valiosos para Windows).
      *(En progreso. Nuevo preset `linux-asan` (`-fsanitize=address,
      undefined`, gcc — el runtime de ASan de clang-18 no está en el
      contenedor de CI; el preset permite override de compilador). Primera
      pasada sobre la suite de tests (6/6) + smoke + test-scene: **3 bugs
      de UB reales encontrados y corregidos**, todos con impacto en ARM
      (Apple Silicon) donde las lecturas desalineadas pueden faltar y los
      optimizadores agresivos pueden romper el overflow con signo:*
      *1) `APISocket::ReceiveProcess` leía `uint16_t`/`int16_t` del buffer*
      *de recepción por `reinterpret_cast` en offsets arbitrarios (posiblemente*
      *impares) — UB de misaligned load. 2) `APISocket::Send` escribía*
      *`uint16_t`/`uint32_t` en el buffer de envío igual. Ambos → helpers*
      *`ReadU16/ReadI16/WriteU16/WriteU32` con `memcpy` (cero coste, el*
      *compilador los baja a un load/store plano). 3) `JvCryption::*
      *JvEncryptionFast` acumulaba el keystream en un `int` con*
      *`rkey *= 2171` que desborda — signed overflow UB; cambiado a*
      *`uint32_t` (wraparound bien definido, bits idénticos porque solo se*
      *usan los bytes `>>8 & 0xff`, así que el cifrado del wire NO cambia —*
      *el test de round-trip cifrado lo confirma). El bug también beneficia*
      *al servidor (JvCryption vive en `shared/`). **Pendiente:** ejercitar*
      *el flujo login→mundo completo bajo ASan requiere assets reales +*
      *servidor local (entorno del usuario en Mac).)*
      *De paso: arreglado un error de compilación gcc-only en `AudioHandle.h`*
      *(miembro `FileReaderHandle FileReaderHandle;` — mismo nombre que el*
      *tipo, mal formado por [basic.scope.class]; clang lo acepta como*
      *extensión, gcc lo rechaza. Renombrado a `ReaderState`).*
* [x] Auditoría de supuestos de 32 bits / orden de bytes en (de)serialización
      de paquetes y formatos (`#pragma pack`, casts de punteros a `DWORD`).
      *(Hecho. Confirmado que el orden de bytes NO es un riesgo real: las tres
      plataformas objetivo (Windows x64, macOS, Linux) son little-endian, y
      `ByteBuffer`/`Packet` (usado por cliente y servidor) serializa siempre
      con `memcpy` byte a byte — no hay `htons`/`ntohl` en ningún punto, así
      que el layout en el wire es idéntico en las tres. El hallazgo real fue
      otro: la MISMA clase de bug de lectura desalineada que motivó esta
      fase (ver arriba, `APISocket`) también vivía en la capa compartida de
      (de)serialización: 1) `ByteBuffer::read<type>(pos)` — el primitivo del
      que depende TODA lectura de campo de paquete (`pkt.read<uint16_t>()`,
      etc., cliente y servidor) — desreferenciaba un `reinterpret_cast<const
      type*>` sobre un offset de byte arbitrario en vez de copiar; 2) las
      funciones legacy `GetShort/GetInt/GetDWORD/GetFloat/GetInt64` en
      `Server/shared-server/utilities.cpp` (usadas por Ebenezer, AIServer,
      VersionManager, ItemManager, Aujard) tenían el mismo patrón; 3)
      `JvCryption::JvDecryptionWithCRC32` leía el CRC final del paquete con
      `*(uint32_t*)(dataout + len - 4)`, con `len` derivado del wire y por
      tanto no garantizado alineado a 4 bytes. Las tres se corrigieron con
      `memcpy` (mismo patrón que `APISocket`, coste cero, el compilador lo
      baja a un load/store plano) — el valor y los bytes en el wire no
      cambian, solo se elimina la UB. Como esta capa la comparten cliente y
      servidor, y los servidores YA corren en macOS/Linux según el README,
      este hallazgo beneficia directamente al entorno del usuario (servidor
      Ebenezer local + cliente macOS). Verificado: build limpio con
      `-Werror` del cliente (Linux) y de los 5 servidores + `Ebenezer.Tests`
      (con ODBC instalado temporalmente para configurar `nanodbc`), suite
      completa 6/6 (cliente) + 4/4 (servidor) en verde, y re-verificado bajo
      el preset `linux-asan` (ASan+UBSan) sin regresiones.)*
* [x] Validación end-to-end del flujo login→mundo contra servidor real
      (Ebenezer local del usuario) — **Hito D alcanzado**: login, lista de
      servidores, selección de nación/personaje, creación de personaje y
      entrada al mundo (movimiento, cámara, UI in-game) funcionando en un
      Mac real. Cuatro bugs reales encontrados y corregidos en el camino,
      todos con el mismo patrón de trabajo: instrumentar con logging
      gateado a POSIX, reproducir en la máquina del usuario, diagnosticar
      con el log exacto, corregir, verificar bajo ASan/UBSan:
      1) **`StaticMemberInit` POSIX solo creaba `s_pProcLogIn`** — tras un
      login exitoso al game server, `ProcActiveSet(s_pProcNationSelect /
      s_pProcCharacterSelect)` recibía `nullptr` y retornaba en silencio
      (síntoma: clic en la lista de servidores "no hacía nada"). Se crean
      ahora los 5 procedimientos igual que Windows (todos compilan en
      POSIX desde T6.8); además se agregó logging de cada transición de
      escena y un warning explícito si `ProcActiveSet` recibe un
      procedimiento nunca creado.
      2) **`SetCurrentDirectory` no normalizaba `\`** — la precarga de
      recursos de personaje en `CGameProcMain::Init` (pantalla "Loading
      Character Data...") construye rutas con el separador histórico
      (`lstrcat(szPathFind, "\\Chr")`); el shim POSIX pasaba ese string tal
      cual a `std::filesystem::current_path`, que trata `\` como carácter
      de archivo normal, así que el cambio de directorio fallaba en
      silencio (retorno sin comprobar) y las 5 etapas de precarga no
      hacían nada. Corregido normalizando `\` → `/` en el shim (mismo
      criterio que `FileIO/PathResolver::NormalizePathSeparators`), con
      test de regresión.
      3) **Texturas no-DXT eran un stub en POSIX** — `CN3Texture::LoadFromFile`
      solo implementaba el formato `.dxt` propio; cualquier textura en
      formato TGA (usado por assets no relacionados a personajes/objetos,
      p.ej. la textura de fases lunares del cielo) devolvía una textura
      nula, y el `__ASSERT(m_pTexture, "Texture load failed.")` de
      `CN3Moon::Init` abortaba toda la aplicación en builds Debug —
      síntoma exacto reportado: la pantalla de carga tras seleccionar
      personaje cerraba la aplicación al empezar a cargar la zona/cielo.
      Se implementó un decodificador TGA propio (24/32bpp, sin comprimir y
      RLE — los únicos subtipos que usan los assets reales) en vez de
      tirar de una librería de imágenes genérica, con 2 tests que fijan el
      contrato de decodificación.
      4) **Los "cuadros rojos sólidos" bajo cada NPC no son un bug del
      port** — `CPlayerBase::Render` tiene una rama `#ifdef _DEBUG` que
      sustituye el modelo real de los NPCs de tipo objeto (decoraciones
      como el muñeco de nieve) por su malla de colisión en rojo sólido
      (`CN3TransformCollision::RenderCollisionMesh`, color `0xffff0000`
      hardcodeado) — herramienta de desarrollo del cliente original que
      nunca se ve en Windows porque el cliente comercial siempre se
      compila en Release. Documentado aquí para que no se confunda con un
      bug real: usar el preset `*-release` (o `RelWithDebInfo`) para
      validación visual, reservando `*-debug` para depuración con
      breakpoints. **Pendiente de investigar** (pueden ser síntomas del
      mismo build Debug, o bugs reales — a confirmar tras cambiar a
      Release): una línea vertical casi imperceptible visible en todas las
      ventanas, y un pequeño desfase de posición en la barra de vida/maná
      del HUD respecto a la esquina superior izquierda.
* [ ] `-Wall -Wextra -Werror` limpio en clang (el proyecto ya lo activa por
      `OPENKO_COMPILE_WARNINGS_AS_ERROR`).
* [ ] Rendimiento: objetivo ≥ paridad con Windows/D3D9 en la misma máquina;
      perfilar con Instruments/`perf`.
* [ ] CI: jobs de macOS/Linux compilan el cliente completo y ejecutan los
      tests de humo (carga de assets + conexión a servidor local).
* [ ] Actualizar README y wiki (setup macOS/Linux del cliente).

### Fase 10 — Port de las herramientas cliente (Option, KscViewer, Launcher) a POSIX (esfuerzo: ~2 sp, fuera del alcance original)

**Objetivo:** las tres apps auxiliares que acompañaban al cliente en Windows
(`src/Client/Option`, `src/Client/KscViewer`, `src/Client/Launcher`) dependen
de MFC — framework de GUI exclusivo de Visual Studio/Win32, sin build en
macOS/Linux — así que nunca compilaron en POSIX (`OPENKO_BUILD_CLIENT_TOOLS`
se fuerza a `OFF` fuera de Windows en el CMake raíz). No formaban parte del
alcance original de este plan (que es "port del cliente WarFare"), pero son
las mismas llamadas a controles de UI estándar (sliders, combos, checkboxes,
diálogos de archivo) una y otra vez — se pueden reimplementar sobre un
toolkit portable sin rehacer nada desde cero.

**Decisión de toolkit: Dear ImGui sobre SDL3 + OpenGL 3.3 core**, en vez de
Qt/GTK. Justificación: SDL3 y el contexto GL 3.3 core ya son dependencias
vivas y probadas por el port del cliente (T6.5); Dear ImGui es MIT, sin
runtime pesado, se vendoriza con el mismo patrón `FetchContent` que
`spdlog`/`SDL3` (`cmake/FindImGui.cmake`, pineado a `v1.92.8`); y para
diálogos de configuración/utilidad (no HUD in-game) el modo inmediato encaja
mejor que una jerarquía de widgets retenida. Qt fue descartado por su huella
de dependencia (mucho más grande que todo el resto del árbol de deps
combinado) y complejidad de licencia (LGPL/comercial); GTK por su API en C
y peor integración en macOS.

Cada herramienta se reimplementa en dos capas, replicando el patrón ya
usado para separar lógica de render en el cliente: un `*Core.h/.cpp`
platform-neutral (sin SDL/ImGui) con la lógica real — parsing de formatos,
lectura/escritura de `.ini`, protocolo de red — testeable sin display; y un
`*MainSDL.cpp` que solo dibuja los widgets y llama a esa lógica. Activado
tras `-DOPENKO_CLIENT_TOOLS_POSIX_EXPERIMENTAL=ON` (nueva opción, mismo
patrón que `OPENKO_CLIENT_POSIX_EXPERIMENTAL`; ya integrada en
`posix-client-base` de `CMakePresets.json`, así que los presets existentes
la traen activada).

* [x] **Option.** Diálogo de configuración (`Option.ini`): calidad de
      textura por LOD (personaje/objeto/terreno), sombras, resolución,
      profundidad de color, distancia de vista, cantidad de efectos,
      sonido (bgm/efectos/duplicados/distancia), cursor por software, modo
      ventana, versión de `Server.Ini`. *(Hecho: `OptionCore.h/.cpp` replica
      byte a byte las secciones/claves y valores por defecto de
      `OptionDlg.cpp` (Windows/MFC) — un `Option.ini` escrito por cualquiera
      de los dos es intercambiable — usando `Platform/PlatformIni.h`, al que
      se le agregó `WritePrivateProfileString` (solo existían los lectores;
      hace update-in-place o append preservando el resto del archivo,
      necesario porque `Option.ini` también lo lee `WarFare`). La detección
      de resoluciones usa `SDL_GetDisplays`/`SDL_GetFullscreenDisplayModes`
      en vez de `EnumDisplaySettings`, con el mismo filtro/orden/fallback a
      la lista hardcodeada. `OptionMainSDL.cpp` dibuja el mismo conjunto de
      controles agrupados por sección; "Apply and Execute" lanza el binario
      `KnightOnLine` autocontenido en vez de `Launcher.exe` (que aún no
      existe en POSIX). Flag `--smoke N` ejercita el round-trip de
      `Option.ini` sin necesitar contexto GL (útil en CI/headless). 12 tests
      nuevos (`tests/Platform/PlatformIni_test.cpp` para el nuevo writer,
      `tests/Option/OptionCore_test.cpp` para la lógica) + verificación
      visual real: capturado con Xvfb + Mesa llvmpipe (mismo método que
      validó el backend GL del cliente en T6.6), los 6 grupos de controles
      renderizan y funcionan.)*
* [ ] **KscViewer.** Visor de `.ksc` (imágenes de splash/loading cifradas,
      formato JPEG propietario) con soporte para exportar a `.jpg` plano.
      Reutiliza el decriptado ya portado en `N3JpegFile`/`JpegFile`
      (libjpeg ya es dependencia POSIX). Pendiente.
* [x] **Launcher.** Version-check real contra VersionManager (mismo puerto
      15100 / IPs de `Server.Ini` que usa la escena de login de `WarFare`)
      y lanzamiento del cliente. Descarga de parches por FTP + extracción
      ZIP (`ZipArchive`, hoy Windows-only) quedan explícitamente diferidas.
      *(Hecho. Antes de escribir código se auditó qué hace *realmente* el
      Launcher de Windows — hallazgo importante: los bitmaps con arte
      (`res/Bkg.bmp` con el caballero, botones `Btn_Connect`/`Btn_join`/...)
      están compilados como recursos pero **ningún código los carga**
      (`grep` exhaustivo sin resultados sobre sus `IDB_*`); el diálogo real
      (`IDD_LAUNCHER_DIALOG` en `Launcher.rc`) es una tira de 279x60 con
      solo una barra de progreso y una línea de estado. Replicar el arte
      habría sido inventar una UI que nunca corrió en Windows, así que se
      portó la de verdad. También se auditó el protocolo: `LauncherDlg.cpp`
      define localmente `LOGIN_REQ=0x03`/`SERVER_LIST=0x05`, que ya NO
      coinciden con `LS_LOGIN_REQ=0xF3`/`LS_SERVERLIST=0xF5` del protocolo
      actual (`shared/packets.h`) — ese camino ya es código muerto contra
      el `VersionManager` real incluso en el binario de Windows, así que no
      tenía sentido para portarlo (la propia escena de login de `WarFare`
      ya es el camino real y funcional de login/lista de servidores). Lo
      que SÍ coincide y funciona: `VERSION_REQ=0x01`/`DOWNLOAD_INFO_REQ=0x02`
      contra `LS_VERSION_REQ`/`LS_DOWNLOADINFO_REQ`, sin cifrado (verificado
      en `Server/VersionManager/User.cpp` — esos dos casos nunca llaman a
      `JvCryption`). `LauncherCore.h/.cpp` reimplementa ese subconjunto
      byte a byte: mismo framing `0xAA55`/`0x55AA` que `CAPISocket::Send`
      (verificado contra `APISocket.cpp` línea por línea), mismos layouts
      `SetString2`/`SetShort` que `VersionManagerApp::SendDownloadInfo`.
      `LauncherMainSDL.cpp` conecta en un hilo de fondo (Asio síncrono,
      mismo patrón sin timeout explícito que `CAPISocket::Connect`), pide
      versión, y si coincide lanza `KnightOnLine` (con una pausa breve
      visible en vez de un salto instantáneo) — si el servidor está por
      delante, pide `DOWNLOAD_INFO_REQ` y muestra la lista de archivos
      pendientes sin descargarlos (el flujo de parcheo FTP/ZIP sigue
      diferido). 11 tests (`tests/Launcher/LauncherCore_test.cpp`) fijan el
      framing/parsing byte a byte contra los layouts reales del servidor.
      **Verificado con un `VersionManager` de juguete real** (socket Python
      hablando el protocolo exacto, no mocks): el intercambio completo
      versión-coincide → auto-lanza `KnightOnLine` (que a su vez abrió un
      contexto GL real y arrancó) y versión-desactualizada → pide y muestra
      la lista de parches, ambos capturados visualmente con Xvfb+Mesa. De
      paso se corrigió que ni `Option` ni `Launcher` reenviaban `--data` al
      `KnightOnLine` que lanzan — ahora ambos lo hacen (solo cuando el
      directorio resuelto es uno real, no el CWD de respaldo). Limpio bajo
      el preset `linux-asan` (ASan+UBSan) ejercitando el socket real.)*
* [x] **Pulido post-port de Option/Launcher** (icono, integración con
      WarFare, bug del botón de lanzar, estilo). *(Hecho:*
      - *Icono de ventana: `Platform/IconDecoder.h/.cpp` decodifica el `.ico`
        original (mismo contenedor que los `.cur` de `CursorDecoder.h`, solo
        cambia el tipo de `ICONDIR` y el par color-count/reserved en vez del
        hotspot) y lo aplica vía `SDL_SetWindowIcon`. `Option.ico`/
        `Launcher.ico` se copian junto al binario en el `POST_BUILD` de cada
        `CMakeLists.txt`. 5 tests contra el `Option.ico` real y datos
        basura.*
      - ***Bug encontrado:** el botón "Apply and Execute" de `Option` (y el
        auto-lanzado de `Launcher`) no hacía nada en macOS porque
        `LaunchWarFareAndExit` solo buscaba `<dir>/KnightOnLine`, pero
        `WarFare` se empaqueta como `MACOSX_BUNDLE` ahí
        (`KnightOnLine.app/Contents/MacOS/KnightOnLine`) — la ventana se
        cerraba en silencio sin lanzar nada. Se extrajo
        `Platform/ProcessLaunch.h` (`FindSiblingExecutable`, que ahora
        también prueba el layout de bundle en `__APPLE__`; `ShellQuote`;
        `LaunchDetached`) compartido entre `Option`, `Launcher` y el nuevo
        hookup de `WarFare`. Además, si el binario no aparece, la ventana ya
        no se cierra: muestra un mensaje de error en rojo con los
        directorios probados (antes fallaba en silencio). 5 tests
        (`tests/Platform/ProcessLaunch_test.cpp`) fijan la búsqueda,
        incluyendo el layout de bundle de macOS. Verificado end-to-end con
        `xdotool` sobre Xvfb: clic real en "Apply and Execute" con y sin el
        binario presente, y con `Launcher` contra el `VersionManager` de
        juguete (rama "up to date" y rama de fallo).*
      - *Integración con WarFare: el menú de salida en juego
        (`UIExitMenu.cpp`, opción "Option") y el mensaje de confirmación de
        salida (`UIMessageBox.cpp`, `BEHAVIOR_EXECUTE_OPTION`) llamaban
        `ShellExecute(..., "Option.exe", ...)` solo bajo `_WIN32`, sin rama
        POSIX — el equivalente no existía. Se agregó
        `Client/WarFare/LaunchOptionTool.h` (usa `Platform/ProcessLaunch.h`
        + `CN3Base::PathGet()` para pasarle a `Option` el mismo directorio
        de datos activo) y se llama desde ambos sitios antes de
        `PostQuitMessage(0)`, igual que en Windows.*
      - *Estilo: reemplazado `ImGui::StyleColorsDark()` por un tema plano y
        redondeado propio (`ApplyModernStyle()` en ambos `*MainSDL.cpp`) —
        el usuario confirmó que no hace falta replicar las fuentes
        nativas de Windows del diálogo original y dio libertad total de
        estilo. Las ventanas de ambas herramientas se agrandaron para
        acomodar el padding nuevo sin recortar contenido.*
      - ***Bug de resolución de pantalla (macOS/POSIX):** tres capas.
        (1) En fullscreen se creaba la ventana con `SDL_WINDOW_FULLSCREEN` a
        secas, que en SDL3 significa "borderless a la resolución del
        escritorio", no la del juego — el motor renderizaba su viewport de
        `iViewWidth x iViewHeight` en una esquina de un framebuffer mucho
        mayor, con el mouse desfasado igual. Ahora se pide el modo de video
        más cercano a la resolución elegida (`SDL_SetWindowFullscreenMode`,
        el equivalente SDL3 del cambio de modo de D3D9), y se fija aunque se
        arranque en ventana para que Alt+Enter caiga en el mismo modo.
        (2) Red de seguridad: tras crear la ventana se compara el tamaño
        real en píxeles con el pedido y, si difieren (modo no disponible,
        ventana recortada por el WM), se adopta el tamaño real en
        `s_Options` ANTES de arrancar el motor, para que viewport, UI,
        proyección e input coincidan siempre con la realidad.
        (3) `RHIDeviceGL` cacheaba el tamaño en píxeles una sola vez al
        crearse — el flip Y de viewport/scissor y el mapeo RHW quedaban
        obsoletos tras un Alt+Enter; ahora se refresca en cada `Present()`.
        Además, la tabla de `GameOptions.cpp` que fuerza la altura según el
        ancho (1280→1024, 1920→1080...) se dejó solo en `_WIN32`: en un
        panel 16:10 convertía 1280x800 en 1280x1024 (más alto que la
        pantalla). SDL/GL acepta cualquier tamaño y el Option portado solo
        ofrece modos reales del display.*
      - ***Bug: los cambios de Option no aplicaban al juego.** Dos capas:
        (1) `LoadGameOptions()` corría ANTES de resolver el directorio de
        datos, así que leía `<CWD-de-lanzamiento>/Option.ini` (inexistente
        al lanzar el .app) y el juego ignoraba silenciosamente la
        resolución/modo ventana editados en Option — reordenado en
        `WarFareMainSDL.cpp`: primero `FindGameDataDir`+chdir, después
        `LoadGameOptions()` (que ahora lee el `Option.ini` real de
        GameData, mismo invariante que Windows con su CWD). (2) En macOS,
        el botón "Option" del menú de salida in-game buscaba el binario
        junto al EJECUTABLE (`Contents/MacOS/` dentro del bundle) cuando
        las herramientas viven junto al `.app` — `LaunchOptionTool` ahora
        también busca en el directorio que contiene el bundle. Con ambos,
        el circuito completo del menú in-game funciona: ESC → Option →
        editar resolución/ventana → "Apply and Execute" → el juego
        relanza con la configuración nueva. Verificado end-to-end en
        Linux (`Option.ini` 800x600+WindowMode=1 → "window created:
        800x600 (windowed)").*
      - *Descubrimiento del layout de distribución: `FindGameDataDir` no
        contemplaba el layout "todos los binarios + `assets/Client` en una
        misma carpeta" ni, en macOS, que la copia de datos del build vive
        DENTRO del bundle (`KnightOnLine.app/Contents/Resources/GameData`)
        donde un binario plano vecino (Option/Launcher) no miraba — por eso
        `Launcher` en macOS caía al CWD y fallaba con "No servers listed".
        Se añadieron los candidatos `<cwd>/assets/Client`,
        `<exeDir>/assets/Client` y (macOS) el `GameData` dentro del bundle
        hermano `KnightOnLine.app`/`Knight OnLine.app`. El error de
        `Server.Ini` vacío ahora muestra la ruta exacta que se intentó leer.
        4 tests nuevos (`tests/Platform/GameDataDir_test.cpp`) + verificación
        end-to-end en un directorio de distribución simulado, con CWD dentro
        y fuera de él.)*

**Aceptación (parcial):** `Option` y `Launcher` compilan, pasan sus tests
(27 en `Platform.Tests`, 7 en `Option.Tests`, 11 en `Launcher.Tests`, 16 en
`WarFareClient.Tests`), y se verificaron visualmente e interactivamente en
Linux headless (Xvfb + `xdotool`) — `Launcher` además contra un servidor de
protocolo real, y `WarFare` compilado con el nuevo hookup de `Option`.
`KscViewer` queda para continuar esta fase.

### Fase 11 — Plan de cierre hacia Hito E ("distribuible")

Con los hitos A–D alcanzados y la migración RHI completada en alcance
cliente (auditoría en F5), esto es lo que queda, en orden recomendado.
Las fases 1 y 2 son el grueso; 3–5 son flecos paralelizables.

**1. F6b — Backend SDL_GPU (Metal en macOS, Vulkan en Linux) — el trabajo
grande restante (~2 sp).** Hoy macOS corre sobre OpenGL deprecado; SDL_GPU
es la salida definitiva. Desglose concreto:

* [x] **T6b.0 — Toolchain de shaders (decisión + andamiaje).** (Hecho, ver Fase 6b.) Dos
      opciones: (a) `SDL_shadercross` integrado al build (HLSL→SPIR-V/MSL/
      DXIL; arrastra DXC y SPIRV-Cross — dependencias pesadas), o
      (b) precompilación offline commiteada: el über-shader HLSL fuente en
      el repo + artefactos `.spv`/`.metallib`/`.dxil` generados por un
      script reproducible (cero dependencias nuevas en el build normal).
      **Recomendación: (b)** — es un único shader que cambia poco; el
      script corre solo cuando se toca el shader.
* [x] **T6b.1 — Über-shader (Vulkan GLSL, ver Fase 6b).** Portar el GLSL actual de
      `RHIDeviceGL` (emulación fixed-function: transform WVP, 2 stages de
      textura con ops D3D9, fog lineal, luces direccionales/puntuales,
      alpha-test, XYZRHW passthrough) a un HLSL único con el mismo layout
      de constantes; verificable offline compilándolo a SPIR-V.
* [x] **T6b.2 — `RHIDeviceSDLGPU`.** (Hecho, ver Fase 6b.) Device + swapchain + render passes;
      caché de pipelines claveada por `RHIStateKey` (ya existe con tests);
      `IRHIVertexBuffer`/`IRHIIndexBuffer`/`IRHITexture` sobre buffers y
      texturas SDL_GPU (upload por transfer buffer); draws UP (el camino
      caliente del motor) e indexados; viewport/scissor con la convención
      de Y correcta; readback de un píxel/frame para los tests de humo
      (paridad con `ReadCenterPixel`/`--dump-frame` del backend GL).
* [x] **T6b.3 — Paridad y default.** (Hecho, ver Fase 6b: paridad
      pixel-perfect + validado in-game sobre Metal; SDLGPU default en macOS
      con probe y fallback a GL.)

**2. F9 — Cierre de estabilización (paralelo, continuo).**

* [ ] ASan/UBSan sobre el flujo login→mundo completo con assets reales
      (la suite de tests y el smoke ya están limpios; falta el in-game).
* [ ] Confirmar los 2 detalles visuales reportados: la línea vertical
      casi imperceptible (pendiente de diagnóstico) y el descuadre del
      HUD (posiblemente resuelto por el fix de resolución/fullscreen —
      pedir confirmación al usuario).
* [ ] CI: jobs de macOS/Linux que compilen el cliente completo y corran
      la suite de tests + smoke en cada PR.
* [ ] README/wiki: instrucciones de setup macOS/Linux (presets de CMake,
      assets/Client, Rosetta/DYLD notes si aplican).

**3. F10 — Flecos de herramientas.**

* [ ] KscViewer (ImGui): descifrar `.ksc` (reutiliza `N3JpegFile`),
      mostrar y exportar `.jpg`.
* [ ] (Opcional) Descarga de parches del Launcher: recomendado HTTP(S) +
      miniz en vez del FTP fiel al original; mantener la UI de progreso
      ya portada.

**4. F1 — Fleco de configuración.**

* [ ] `Option.ini` por-usuario (XDG config / Application Support) con
      fallback de lectura al directorio de datos, sin romper la
      convención Windows.

**5. Hito E — Distribuible.**

* [ ] macOS: `.app` firmada ad-hoc (`codesign --deep -s -`) + DMG/zip;
      Linux: tarball con rpath `$ORIGIN` (ya configurado) o AppImage.
* [ ] CI verde en ambos + smoke de arranque del binario empaquetado.

---

## 4. Orden, dependencias e hitos

```
F0 Build/CI ─► F1 Fundaciones ─► F2 Red (Asio) ──────────────┐
                    │                                         │
                    ├─► F3 SDL ventana+input ─► F6 Backend GL ├─► F7 Texto/IME ─► F8 Empaquetado ─► F9 Estabilización
                    │                              ▲          │
                    └─► F4 Residuos D3DX ─► F5 RHI ┘          └─► F6b Backend SDL_GPU (paralelo a F7)
                                                              
   Hito A: "headless" (F1+F2): assets cargan y el socket habla con Ebenezer en macOS - [x]
   Hito B: "ventana viva" (F3): ventana + input en macOS - [x]
   Hito C: "login visible" (F5+F6 parcial): escena de login renderiza en macOS - [x]
   Hito D: "jugable" (F6+F7): entrar al mundo, moverse, chatear - [x] alcanzado
           (validado en Mac real contra Ebenezer local: login, lista de
           servidores, selección/creación de personaje, entrada al mundo,
           movimiento y HUD in-game; ver F9 para los 4 bugs que lo
           bloqueaban y los 2 detalles visuales pendientes de confirmar)
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
