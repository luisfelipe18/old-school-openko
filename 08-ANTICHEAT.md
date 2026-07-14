# 08 — Anticheat: por qué NO es parte de este trabajo

Carpeta relacionada: `anti-cheat-system/`.

## El malentendido a evitar

El proveedor turco del servidor trabaja con el **binario retail** del cliente
(`KnightOnline.exe`, sin fuente). Ese binario espera ser lanzado por
`Launcher.exe` a través de un anticheat (originalmente **Xigncode**) y se cierra
si no fue invocado así. Lo que ellos hacen es:

1. Editar hexadecimalmente el `KnightOnline.exe` retail para que **no se cierre**
   cuando lo lanza su propio anticheat en vez de Xigncode.
2. Ajustar el **"crypto"** (cifrado de red) hexadecimalmente para que coincida
   con su servidor.

## Por qué NO nos aplica

**Nuestro cliente se compila desde código fuente** (`old-school-openko/`). No es
el binario retail. Por lo tanto:

- **No lleva Xigncode ni ningún anticheat adentro.** No hay comprobación de
  "¿me lanzó el launcher correcto?" que haya que bypassear. El chequeo
  simplemente no existe en el código open-source.
- Toda la edición hexadecimal que describe el video en turco es para hacerle a
  un binario cerrado lo que nosotros **hacemos gratis** teniendo la fuente: si
  quisiéramos cambiar un comportamiento, editamos el `.cpp` y recompilamos.

Conclusión: **la carpeta `anti-cheat-system/` y todo el tema del anticheat se
ignoran para este proyecto.** No hay que portarlo, integrarlo, ni bypassearlo.

## Lo único que SÍ importa de todo eso: el "crypto"

De las dos ediciones hexadecimales que hacen, solo una nos concierne: el
**cifrado de paquetes** ("crypto"). Y no lo tocamos a nivel binario: lo
**leemos del código fuente del servidor** (`game-server/`) y lo **escribimos en
el código fuente del cliente** (`JvCryption` en `old-school-openko/src/shared/`).
Ver documento 05.

## Si en el futuro se quisiera conectar a un servidor con anticheat activo

Fuera de alcance por ahora. Nuestro servidor de pruebas (el 24xx que portaremos,
documento 04) lo controlamos nosotros y no necesita anticheat. Un cliente
open-source no está pensado para conectarse a servidores oficiales protegidos,
y este proyecto es de preservación/interoperabilidad sobre infraestructura
propia.
