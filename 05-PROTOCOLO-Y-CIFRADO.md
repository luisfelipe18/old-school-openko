# 05 — Protocolo de red y cifrado

Este es el corazón del trabajo de cliente. Si el cliente no habla el protocolo y
el cifrado 24xx exactamente como el servidor espera, **no pasa nada** — ni el
login. La buena noticia: el código del servidor (`game-server/`) **define todo
esto de forma autoritativa**. No se adivina; se lee.

## El cifrado ("crypto") — el primer bloqueo

Cuando en el proyecto turco hablan de "crypto" y de editar el binario
hexadecimalmente, se refieren (casi con seguridad) al **cifrado de los paquetes
de red**, no al anticheat. En el motor open-source esto existe como la clase
**`JvCryption`** (en `old-school-openko/src/shared/`).

- El cliente cifra lo que envía y descifra lo que recibe con este esquema; el
  servidor hace lo simétrico.
- Lo que ellos ajustan en el binario retail (claves, constantes, variante del
  algoritmo) nosotros simplemente lo **escribimos en el código fuente** del
  cliente open-source, copiándolo del servidor 24xx.

**Tarea crítica**: localizar en `game-server/` la implementación de
cifrado/descifrado de paquetes y compararla con
`old-school-openko/src/shared/` (`JvCryption` y afines). Determinar:

- ¿Es el mismo algoritmo que el 12xx, con distintas claves/constantes?
- ¿Cambió el esquema por completo?
- ¿Hay un handshake inicial que intercambia una clave de sesión? (En el 12xx, el
  `version check` devuelve una clave de cifrado — ver el flujo de login.)

Hasta que el cliente cifre igual que el servidor 24xx, nada más funciona. Por
eso es el **primer entregable** del lado cliente.

## La serialización (formato en el cable)

Del port 12xx sabemos (y probablemente siga igual en 24xx, verificar):

- **Todo little-endian.** Las tres plataformas objetivo lo son.
- `ByteBuffer` / `Packet` serializan con **`memcpy` byte a byte**. **No hay
  `htons`/`ntohl`** en ningún punto — el layout en el cable es el layout en
  memoria.
- Primitivos: `pkt.read<uint16_t>()`, `MP_AddByte/Word/Short/String`, etc.
- Cuidado con lecturas desalineadas: siempre `memcpy`, nunca `*(T*)ptr` sobre
  offsets arbitrarios (UB en ARM).

## Los opcodes

En el 12xx los opcodes son constantes `WIZ_*` (game server) y `LS_*` (login
server), definidos en `PacketDef.h`. El servidor 24xx tendrá su propio set,
seguramente ampliado. Hay que:

1. Extraer de `game-server/` la lista completa de opcodes y, para cada uno, la
   estructura de su request/response (qué campos, qué tipos, en qué orden).
2. Compararla contra el `PacketDef.h` del cliente 12xx.
3. Producir un **diff de protocolo**:
   - Opcodes nuevos (funcionalidad nueva).
   - Opcodes que cambiaron de tamaño/formato (campos añadidos/removidos).
   - Opcodes que desaparecieron.

## Fase 0 del lado cliente (se puede hacer solo leyendo `game-server/`)

Sin correr nada, produce:

1. **Documento de cifrado**: algoritmo, claves/constantes, handshake de clave de
   sesión si lo hay, y el diff contra `JvCryption` del 12xx.
2. **Documento de protocolo**: tabla de opcodes 24xx con sus estructuras, y el
   diff contra el 12xx.

Estos dos documentos son exactamente lo que después guía la actualización del
cliente (documento 06).

## Dónde mirar en cada repo

- **Servidor 24xx** (`game-server/`): busca la clase de cifrado, el
  `PacketDef`/enum de opcodes, y el loop de recepción/parseo de paquetes
  (`GameServer` y `Login`).
- **Cliente 12xx** (`old-school-openko/`):
  - `src/shared/` → `JvCryption` (cifrado).
  - `src/Client/WarFare/PacketDef.h` → opcodes.
  - `src/Client/WarFare/APISocket.cpp` → envío/recepción y (de)serialización.
  - `src/Client/WarFare/GameProcLogIn_1298.cpp` → flujo de login/handshake.
  - `src/Client/WarFare/GameProcMain.cpp` → handlers de paquetes in-game.

## Validación (opcional, cuando el servidor corra)

Con el servidor 24xx levantado, se puede capturar el tráfico del **cliente
retail 24xx** ↔ servidor como "verdad de campo" para casos ambiguos. Pero con el
código del servidor a la vista, esto es la excepción, no la regla. Si se usa,
conviene un loop estructurado: el humano anota "probando funcionalidad X" antes
y "terminada X" después, para acotar qué paquetes corresponden a qué acción.
