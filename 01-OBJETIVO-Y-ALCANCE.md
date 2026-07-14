# 01 — Objetivo y alcance

## El punto de partida

Knight Online es un MMORPG de 2004. Con los años el juego recibió muchísimas
actualizaciones: más niveles, nuevas clases/poderes, armas, armaduras, ítems,
eventos, sistemas (upgrade, power-up store, guerras, clanes, etc.). Cada gran
versión del juego se identifica por un número de build (ej. 1298 = "12xx",
2400+ = "24xx").

Tenemos tres piezas de una versión **moderna (24xx)**:

- **Cliente 24xx**: binarios (`KnightOnline.exe`, `Launcher.exe`, `Option.exe`)
  + todo el contenido distribuible (assets: modelos, texturas, íconos, UI, FX,
  sonidos, zonas). → carpeta `client/`
- **Servidor 24xx**: código fuente completo (solo se usan `Login` y
  `GameServer` en esta versión). → carpeta `game-server/`
- **Base de datos 24xx**: completa, con tablas, procedimientos almacenados, etc.
  (MSSQL). Corre en un servidor remoto.

Y tenemos una pieza **open-source de una versión antigua (12xx)**:

- **`old-school-openko/`**: un port del cliente **1298 (12xx)** a macOS/Linux,
  hecho sobre el código open-source del motor. Es donde se hizo todo el trabajo
  pesado de portabilidad. Ver documento 03.

## Lo que se quiere lograr

**Actualizar el cliente open-source 12xx (`old-school-openko/`) para que sea
compatible con el servidor 24xx**, produciendo un cliente moderno que corra
nativo en macOS/Linux (además de Windows).

El cliente 24xx retail existe como binario para Windows, pero **no tenemos su
código fuente** — por eso no se puede simplemente recompilar para Mac. La
estrategia es tomar el cliente open-source 12xx (que sí tiene fuente y ya está
portado) y llevarlo hacia adelante hasta el protocolo/contenido de 24xx.

## Por qué es viable (las tres patas)

En un proyecto así, lo que normalmente mata el intento es no tener el arte. Aquí
tenemos las tres cosas que hacen falta:

1. **Servidor fuente** (`game-server/`): define el protocolo de forma
   autoritativa — opcodes, estructuras de paquetes, fórmulas y, sobre todo, el
   **cifrado**. No hay que adivinar por ingeniería inversa ciega: se lee el
   código.
2. **Assets del cliente 24xx** (`client/`): modelos, íconos, UI, FX de todo el
   contenido nuevo. Esto es lo que no se puede inventar, y lo tenemos.
3. **Base de datos** (24xx): define el contenido — IDs, stats, nombres de
   ítems/skills/eventos.

## Alcance realista

Esto **no es un one-shot**. Un salto 12xx → 24xx son años de parches
acumulados. Es un proyecto largo, por incrementos. Pero es **tratable** porque:

- El port de plataforma (D3D9→RHI, Win32→SDL, Winsock→Asio, texto→FreeType) ya
  está hecho en `old-school-openko/` (documento 03).
- El port del servidor a Linux **no empieza de cero**: el mismo motor ya se
  portó a Linux en `old-school-openko/src/Server/` (documento 04).
- El contenido nuevo ya existe como assets (`client/`).

## Sub-objetivo inmediato

Antes de tocar el cliente, el bloqueo práctico es tener un **servidor 24xx que
corra en Linux** para poder iterar el ciclo cliente↔servidor sin depender de una
máquina Windows. De ahí que el primer gran trabajo sea el **port del servidor
24xx a Linux** (documento 04), reusando la capa de plataforma ya existente.

## Lo que NO es parte de este trabajo

- **El anticheat / Xigncode / la edición hexadecimal del binario retail.** No
  aplica a un cliente compilado desde fuente. Ver documento 08.
- Redistribuir binarios o assets con copyright. Es un proyecto privado de
  preservación/interoperabilidad; nada se publica.
