# 02 — Estructura de carpetas

El desarrollo local ocurre dentro de una carpeta padre que contiene cuatro
subcarpetas. Estos documentos `.md` deberían estar accesibles desde esa carpeta
padre (o cópialos allí). Todas las rutas en los documentos se refieren a estas
carpetas por su nombre.

```
<carpeta-padre>/
├── anti-cheat-system/     # (1) Sistema anticheat — NO se usa (ver doc 08)
├── client/                # (2) Cliente 24xx: binarios + assets/contenido
├── game-server/           # (3) Código fuente del servidor 24xx (Windows)
└── old-school-openko/     # (4) Port open-source del cliente 12xx (macOS/Linux)
```

## (1) `anti-cheat-system/`

Sistema anticheat que el proveedor del servidor usa con el cliente retail
(reemplaza a Xigncode). **No forma parte de este proyecto.** Nuestro cliente se
compila desde código fuente y no lleva anticheat. Ver documento 08 para el
detalle de por qué se ignora y qué es realmente el "crypto" que mencionan.

## (2) `client/` — Cliente 24xx (binarios + assets)

Contiene el cliente retail 24xx completo:

- **Binarios**: `KnightOnline.exe`, `Launcher.exe`, `Option.exe` (Windows, sin
  fuente).
- **Contenido distribuible** (carpetas típicas del juego):
  `fonts`, `fx`, `icon`, `info`, `intro`, `item`, `KnightMovie`, `Misc`,
  `npcimg`, `Object`, `Scene`, `SHC`, `Snd`, `symbol`, `UI`, `Zones`.

**Valor clave**: estos assets son el contenido nuevo (modelos, íconos, UI, FX de
armas/armaduras/skills/eventos 24xx). Es lo que se puede reusar para que el
cliente actualizado tenga el arte moderno. Los formatos son los propios del
motor Knight Online (`.dxt`, `.n3chr`, `.n3cplug`, `.n3cpart`, `.ksc`, `.uif`,
etc.), posiblemente con versiones de formato más nuevas que las que lee el
cliente 12xx — habrá que verificar y, si cambió, actualizar los lectores.

> Herramientas útiles que ya existen en `old-school-openko/`: **AssetExplorer**
> y **KscViewer** sirven para inspeccionar estos assets y ver qué formato usan.

## (3) `game-server/` — Código fuente del servidor 24xx

Código fuente completo del servidor de esa versión 24xx. En esta versión solo se
usan dos ejecutables: **`Login`** y **`GameServer`** (a diferencia del 12xx
open-source, que separa en Ebenezer / AIServer / VersionManager / ItemManager /
Aujard).

- Escrito para **Windows** (Winsock, ODBC de Windows, APIs Win32).
- Origen: comprado a un desarrollador turco hace años. "Ha de estar basado en el
  mismo código fuente" del motor original, con bastante ingeniería inversa
  encima. **Confirmar si es un fork del mismo Ebenezer** comparando con
  `old-school-openko/src/Server/` (documento 04).
- Es la **fuente autoritativa del protocolo y del cifrado** (documento 05).

## (4) `old-school-openko/` — El port open-source 12xx

El repositorio en el que ya se trabajó. Cliente 1298 (12xx) portado a
macOS/Linux, con Windows aún funcionando. Los servidores del mismo motor (12xx)
ya compilan en Linux. **Es la base de todo lo demás.** Ver documento 03 en
profundidad, y sus propios docs internos:

- `old-school-openko/docs/PORT_POSIX_PLAN.md` — plan completo del port.
- `old-school-openko/docs/PORT_POSIX_CONTEXT.md` — reglas, gotchas, build/test.

## Relación entre las carpetas

- De **`game-server/`** sacamos el **protocolo + cifrado** (para el cliente) y
  lo **portamos a Linux** reusando `old-school-openko/src/Platform`.
- De **`client/`** sacamos los **assets 24xx** para el contenido nuevo.
- La **DB 24xx** la conecta el servidor (documento 07).
- Todo el desarrollo de cliente se hace **dentro de `old-school-openko/`**,
  llevándolo del protocolo 12xx al 24xx.
