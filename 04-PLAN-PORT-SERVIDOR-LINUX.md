# 04 — Plan: portar el servidor 24xx a Linux

## Por qué (y por qué NO empieza de cero)

El código del servidor 24xx (`game-server/`) está escrito para Windows. Para
poder correrlo como servidor de pruebas en el entorno de desarrollo (Linux) y
así iterar el ciclo cliente↔servidor sin depender de una máquina Windows, hay
que portarlo.

**La clave**: en `old-school-openko/src/Server/` **ya se portó a Linux el mismo
motor de servidor en su versión 12xx** (Ebenezer, AIServer, etc.). Si el
servidor 24xx es un fork del mismo Ebenezer —muy probable— entonces el port a
Linux **no es reescribir, es diffear y reaplicar** la capa de shims que ya
existe. Eso puede reducir el trabajo de meses a semanas.

## Regla de oro

**Reutiliza, no reinventes.** Antes de escribir cualquier shim de Windows,
busca si ya existe en `old-school-openko/`:

- `old-school-openko/src/Platform/` — shims de Win32 (I/O, strings, tiempo,
  tipos, tokens D3D). Casi todo lo Win32 no-gráfico que necesita un servidor
  está aquí.
- `old-school-openko/src/Server/shared-server/` — utilidades de servidor ya
  portadas (incluye las funciones `GetShort/GetInt/GetDWORD/...` con `memcpy`).
- `old-school-openko/src/shared/` — código compartido cliente+servidor,
  **incluyendo el cifrado `JvCryption`** (ver doc 05).
- El uso de **nanodbc** para ODBC (reemplaza el ODBC de Windows) y **Asio**
  para red (reemplaza Winsock) ya está resuelto y se puede copiar el patrón.

## Fase 0 — Evaluación (SIN escribir código)

Entrega un informe antes de tocar nada:

1. **¿Es un fork del mismo Ebenezer?** Compara `game-server/` con
   `old-school-openko/src/Server/Ebenezer/`: nombres de clases (`CUser`,
   `CNpc`, `CGameProcedure` del lado server, etc.), estructura de carpetas,
   nombres de archivos. Cuantifica el solapamiento.
2. **Inventario de dependencias Windows-only** en `game-server/`:
   - Red: Winsock (`WSAStartup`, `WSAAsyncSelect`, `select`, sockets).
   - DB: ODBC de Windows (`SQLConnect`, `SQLExecDirect`, ...).
   - Sistema: Win32 (threads `CreateThread`, `CRITICAL_SECTION`, registro,
     `GetPrivateProfileString` para `.ini`, timers, `HANDLE`).
   - Compilador: MSVC-ismos (`#pragma`, `__declspec`, tipos `__int64`, etc.).
3. **Mapa de reuso**: qué shim de `old-school-openko/src/Platform` cubre cada
   dependencia, y qué falta crear nuevo.
4. **Plan por fases** con un primer hito medible: **"compila y enlaza en
   Linux"** (aunque no conecte a la DB todavía).

## Fase 1 — Compila y enlaza

- Montar CMake para `Login` y `GameServer` (mirar los `CMakeLists.txt` de
  `old-school-openko/src/Server/*` como plantilla).
- Reemplazar includes Win32 por los shims de `src/Platform`.
- Sustituir Winsock por Asio y ODBC-Windows por nanodbc, copiando el patrón del
  12xx.
- Objetivo: linkea un binario, aunque falle en runtime.

## Fase 2 — Conecta a la DB

- Configurar nanodbc contra la DB 24xx (MSSQL). Ver documento 07 para
  credenciales y seguridad.
- Que el servidor levante, lea sus tablas de configuración y quede escuchando.

## Fase 3 — Corre y sirve

- `Login` acepta conexiones y responde el handshake / version check.
- `GameServer` acepta un login y mete al jugador al mundo.
- Este es el servidor de pruebas contra el que se valida el cliente
  actualizado.

## Notas técnicas heredadas del port 12xx

- **Todo little-endian**; la serialización es `memcpy` byte a byte (ver doc 05).
  No introducir `htons`/`ntohl`.
- **Lecturas desalineadas**: usa `memcpy`, nunca `*(T*)ptr` sobre offsets
  arbitrarios (rompe en ARM, y el CI del 12xx corre con ASan/UBSan).
- **Encoding**: nombres/strings de assets en CP949; convertir en fronteras.
- El servidor 12xx open-source corre con `nanodbc`; para configurarlo en CI se
  instala ODBC temporalmente. Mira cómo lo hace el repo.

## Alternativa: correr el servidor en Windows

Si el port a Linux resulta más caro de lo esperado, hay un **plan B**: correr el
servidor 24xx tal cual en Windows (o una VM) y solo **leer su código fuente**
para el trabajo de protocolo del cliente. Es más lento para iterar, pero
desbloquea el trabajo de cliente sin esperar al port completo del servidor.
Decide según lo que revele la Fase 0.
