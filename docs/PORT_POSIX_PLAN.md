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

* [ ] `CMakeLists.txt` raíz: sustituir el force-OFF de `OPENKO_BUILD_CLIENT`
      en no-Windows por una opción experimental
      `OPENKO_CLIENT_POSIX_EXPERIMENTAL` (default OFF) que permita activarlo.
* [ ] Presets de CMake (`CMakePresets.json`): `macos-arm64-debug`,
      `macos-arm64-release`, `linux-clang-debug`, `linux-gcc-release`
      (generador Ninja, `ccache` si existe).
* [ ] Integrar deps POSIX: verificar que `deps/openal-soft`, `deps/mpg123`,
      `deps/zlib`, `deps/asio` (variantes no-msvc con
      `fetch-and-build-wrappers`) compilan en macOS/Linux; añadir `SDL3`,
      `freetype` y `glad` (find_package con fallback a FetchContent, para que
      Homebrew/apt sean opcionales).
* [ ] GitHub Actions: job `client-macos` (macos-15, Apple Clang) y
      `client-linux` (ubuntu-24.04, clang-18) que por ahora compilan solo los
      targets que vayan quedando portables (empezando por `MathUtils`,
      `FileIO`, `shared`). El job crece fase a fase.
* [ ] Documentar setup: `brew install cmake ninja sdl3 freetype openal-soft mpg123`
      / `apt install build-essential cmake ninja-build libsdl3-dev libfreetype-dev libopenal-dev libmpg123-dev libgl1-mesa-dev`.

**Aceptación:** CI verde en 3 SOs con los targets portables actuales;
Windows intacto.

### Fase 1 — Fundaciones de portabilidad, sin nada visual (esfuerzo: ~2 sp)

**Objetivo:** que `N3Base` (menos render/UI) y la lógica no visual de
`WarFare.Core` compilen en macOS con clang. Es la fase "mecánica" grande.

* [ ] Crear `src/Platform/` con:
  * `PlatformTypes.h`: en Windows incluye lo de siempre; en POSIX define los
    tipos que hoy se filtran por interfaces (`BOOL`, `DWORD`, `POINT`, `RECT`,
    `HWND` → handle opaco de ventana propia `WindowHandle`).
  * `PlatformTime.h`: `PlatformTickMs()` sobre `std::chrono::steady_clock`;
    migrar los 17 usos de `timeGetTime()` y el reloj de `N3Base.cpp`.
  * `PlatformString.h`: reemplazos de `lstrcpy/lstrcat/wsprintf` por
    `std::string`/`fmt` (el repo ya usa fmt vía spdlog); `_MAX_PATH` →
    `PATH_MAX`/`std::filesystem`.
  * `PlatformDebug.h`: `TRACE`/`__ASSERT`/`OutputDebugString` → spdlog + `assert`.
* [ ] **Sacar `<winsock2.h>` del PCH** (`WarFare/StdAfx.h`): es lo que mete
      headers de Windows en cada TU. Dejar el PCH con `My_3DStruct.h` +
      `warfare_config.h` y que red se incluya solo donde se usa.
* [ ] `GetCurrentDirectory`/rutas: migrar `CN3Base::PathSet/PathGet` y la carga
      de `Option.ini` a `std::filesystem`; en macOS/Linux, ubicar la config de
      usuario en `~/Library/Application Support/OpenKO/` y
      `${XDG_CONFIG_HOME:-~/.config}/openko/` respectivamente (con fallback al
      directorio del ejecutable para no romper la distribución portable).
* [ ] **Resolución case-insensitive de assets** en
      `FileIO`/`N3BaseFileAccess`: los archivos del juego mezclan mayúsculas
      (`Item.tbl` vs `item.tbl`). Implementar un resolvedor que, si la ruta
      exacta no existe, escanee el directorio ignorando mayúsculas (con caché).
      Imprescindible en Linux; recomendable en macOS (APFS puede ser
      case-sensitive).
* [ ] **Capa de encoding**: utilidades `Cp949ToUtf8`/`Utf8ToCp949` sobre
      `iconv`; auditar strings literales no-ASCII en el código (comentarios en
      coreano ya están en archivos UTF-8, verificar con `file`/`iconv -c`).
* [ ] Barrido de llamadas sueltas Win32 en lógica de juego (`MessageBox`,
      `GetCursorPos`, `Sleep`, `GetTickCount`, …) → PAL o SDL (stub temporal).
* [ ] CMake: nuevo target `N3Base_portable` (o `#ifdef` de exclusión) que
      compile en POSIX todo N3Base **excepto** `N3Eng`, `DFont`, `N3UIEdit`,
      `N3Texture` y demás render (se incorporan en fases 5–7).

**Aceptación:** en macOS compilan `MathUtils`, `FileIO`, `shared`,
`JpegFile`, `ZipArchive` y el subconjunto no-render de `N3Base`; un test de
humo carga un `.n3chr`/`.n3shape` real y valida sus datos (esto además
protege el formato binario: `__Vector3` debe seguir siendo 12 bytes,
`D3DCOLOR` → `uint32_t` ARGB).

### Fase 2 — Red: de `WSAAsyncSelect` a Asio (esfuerzo: ~1.5 sp)

**Objetivo:** `CAPISocket` multiplataforma y desacoplado de la ventana.

* [ ] Reescribir `CAPISocket` (`Connect/Disconnect/Send/Receive/ReConnect`)
      sobre `asio::ip::tcp::socket` no bloqueante; conservar API pública,
      buffers y criptografía (`JvCryption`) intactos — el protocolo NO cambia.
* [ ] Sustituir el modelo push (`WM_SOCKETMSG` → `WndProc`) por polling: un
      `CAPISocket::Poll()` llamado una vez por tick desde `CGameProcedure`
      drena lecturas y detecta desconexión (`ReportServerConnectionClosed`).
* [ ] Eliminar `WM_SOCKETMSG` de `WarFareMain.cpp` y `WSAStartup`/`ioctlsocket`.
* [ ] Adoptar la misma implementación en Windows (menos código, un solo camino).

**Aceptación:** test de integración contra Ebenezer/VersionManager local
(ya compilan en macOS/Linux): handshake de login + eco de paquetes cifrados
en las tres plataformas. Windows sigue conectando al servidor oficial de
pruebas side-by-side.

### Fase 3 — Ventana, bucle principal y entrada con SDL3 (esfuerzo: ~2 sp)

**Objetivo:** ventana abierta y entrada funcionando en macOS/Linux; game loop
portado. (Aún sin render: pantalla negra con clear color.)

* [ ] Nuevo `WarFareMainSDL.cpp` (o `WarFareMain.cpp` unificado): `main()` +
      `SDL_CreateWindow` (+ flags high-DPI); reproducir la lógica de
      `CreateMainWindow` (modo ventana/fullscreen desde `Option.ini`;
      `ChangeDisplaySettings` → `SDL_SetWindowFullscreen` con
      `SDL_DisplayMode`).
* [ ] Bucle: `SDL_PollEvent` + `TickActive()/RenderActive()` en idle,
      replicando el patrón `PeekMessage` actual. Mapear eventos:
      foco (`WM_ACTIVATE` → `SDL_EVENT_WINDOW_FOCUS_*`, incluida la salida en
      fullscreen al perder foco), cierre (`WM_CLOSE` → lógica de
      `RequestExit`/timer de combate), rueda (`WM_MOUSEWHEEL` →
      `SDL_EVENT_MOUSE_WHEEL` con el mismo enrutado a UI/zoom de cámara).
* [ ] Reimplementar `CLocalInput` sobre `SDL_GetKeyboardState` + eventos de
      ratón: tabla estática `SDL_Scancode → DIK_*` (los 256 códigos), estados
      press/pressed/repeat y detección de doble clic/drag idénticas.
      **La API pública de `CLocalInput` no cambia** → los 150 usos de `DIK_*`
      en gameplay quedan intactos.
* [ ] Cursores: decodificar los `.cur` existentes (formato ICO/CUR, parser
      trivial o `stb_image`) → `SDL_CreateColorCursor`; respetar la opción
      `bWindowCursor` vs cursor software (`CGameCursor`).
* [ ] `_IsKeyDown(VK_MENU)` y demás `VK_*` puntuales → PAL/SDL.

**Aceptación:** en macOS se abre la ventana 1024×768, alterna
ventana/fullscreen, y una build de diagnóstico registra teclado (como códigos
DIK), ratón, rueda, foco y cierre limpio con desconexión de sockets.

### Fase 4 — Utilidades D3DX residuales y carga de texturas (esfuerzo: ~1 sp)

**Objetivo:** eliminar `d3dx9.h` de `My_3DStruct.h` y del código común, para
que la única dependencia gráfica restante sea la interfaz del device.

* [ ] `My_3DStruct.h`: quitar `#include <d3dx9.h>`; definir localmente los
      tipos de datos puros que se usan (`D3DCOLOR` → `uint32_t` ARGB con
      helpers, `_D3DCOLORVALUE` → struct propio de 4 floats, formatos de
      textura → enum RHI). Mantener layout binario exacto (assets).
* [ ] Reemplazar `D3DXLoadSurfaceFromSurface` (8 usos, reescalado/conversión de
      superficies en `N3Texture`/`DFont`) por `stb_image_resize2` +
      conversores de formato propios; `D3DXCreateTextureFromFileEx` (1 uso) por
      el loader propio (los `.dxt` del juego ya se cargan con código propio;
      los DXT1/3/5 se suben tal cual como texturas comprimidas).
* [ ] `D3DXGetErrorString` → tabla de errores propia del RHI.

**Aceptación:** `grep -r "d3dx" src/N3Base src/Client` solo aparece en el
backend D3D9 de Windows (aislado); Windows compila y renderiza igual.

### Fase 5 — RHI: interfaz de render abstracta (esfuerzo: ~3 sp)

**Objetivo:** que ningún archivo de juego hable con `LPDIRECT3DDEVICE9`.
Es el refactor más grande pero es *mecánico*: D3D9 fixed-function tiene una
superficie de API pequeña y repetitiva.

* [ ] Inventariar todos los usos de `CN3Base::s_lpD3DDev` (SetRenderState,
      SetTextureStageState, SetTransform, SetTexture, SetStreamSource,
      DrawPrimitive[UP], DrawIndexedPrimitive[UP], Create*Buffer,
      CreateTexture, viewport, luces, material, fog, Clear/BeginScene/Present).
* [ ] Definir `IRHIDevice` (+ `RHITexture`, `RHIVertexBuffer`,
      `RHIIndexBuffer`) que calque esa superficie 1:1 — deliberadamente con
      la *semántica de D3D9* (render states, texture stages, FVF) para que la
      migración de los call-sites sea sustitución textual y revisable.
* [ ] Diseñar desde el día uno la **clave de estado de pipeline** dentro de la
      RHI (hash de blend/depth/cull/formato de vértice/config de stages):
      el backend GL la ignora (aplica estados sueltos), pero es el requisito
      para que el backend SDL_GPU (fase 6b) pueda cachear pipeline-objects
      sin re-tocar el código de juego.
* [ ] Backend 1: `RHIDeviceD3D9` (Windows) — envoltorio fino sobre el device
      actual. **Hito clave: Windows corriendo 100% sobre el RHI**, lo que
      valida la abstracción antes de escribir una línea de GL.
* [ ] Migrar módulo a módulo: `N3Base` render core → terreno → personajes →
      efectos → UI. PRs pequeños, screenshot-diff manual en Windows.

**Aceptación:** cliente Windows funciona igual que master usando solo el RHI;
`LPDIRECT3D*` no aparece fuera de `RHIDeviceD3D9`.

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
