# 09 — Metodología y convenciones

## Cómo trabajar (heredado del port 12xx)

1. **Incremental y siempre compilando.** Cada cambio deja el árbol en un estado
   verificable. Nada de refactors gigantes de un tirón.
2. **No romper Windows.** El cliente 12xx mantiene el camino nativo Windows con
   `#ifdef _WIN32`. Todo lo POSIX va detrás de shims. El servidor debe seguir el
   mismo criterio.
3. **Lee el código, no adivines.** El servidor (`game-server/`) es la fuente
   autoritativa del protocolo. La DB es la referencia del contenido. Los assets
   (`client/`) son el arte. No inventes formatos: verifícalos.
4. **Reutiliza lo ya hecho.** Antes de escribir un shim de Windows, busca en
   `old-school-openko/src/Platform` y `src/shared`. Casi todo lo no-gráfico ya
   está resuelto.

## Verificación estándar

Siempre, antes de dar algo por hecho, en `old-school-openko/`:

```
cmake --build build/linux-clang-debug -j        # compila
cd build/linux-clang-debug && ctest --output-on-failure   # tests
SDL_VIDEODRIVER=dummy ./bin/Debug/KnightOnLine --smoke 30 # smoke headless
```

Para el servidor: compila + corre + conecta a la DB (copia local) + acepta un
login. Ver documentos 04 y 07.

Cuando el cambio tenga superficie visual/de red real, valida el flujo de verdad
(login → mundo) contra el servidor 24xx, no solo los tests.

## El ciclo cliente ↔ servidor de pruebas

Una vez que el servidor 24xx corra (en Linux idealmente, o en Windows como plan
B):

- Levantar `Login` + `GameServer` + DB (copia local).
- Correr el cliente open-source actualizado.
- Si hace falta capturar tráfico del cliente **retail** 24xx como referencia,
  usar un loop estructurado: el humano anota **"probando funcionalidad X"**
  antes y **"terminada X"** después, para acotar qué paquetes corresponden a qué
  acción. Pero con la fuente del servidor a la vista, esto es la excepción.

## Convenciones de git

- Ramas de trabajo con prefijo `feature/`. Commits descriptivos.
- No mezcles el trabajo de servidor y el de cliente en el mismo commit.
- No subas credenciales, `.bak` de la DB, ni binarios/assets con copyright.
  Verifica `.gitignore`.
- El repo `old-school-openko` es open-source (fines académicos): no metas ahí
  código propietario del servidor turco ni assets retail. Manténlos en sus
  carpetas (`game-server/`, `client/`), fuera del repo público.

## Separación de lo público y lo privado

- **`old-school-openko/`** es público (open-source). Todo lo que se commitee ahí
  se puede publicar.
- **`game-server/`**, **`client/`**, **`anti-cheat-system/`** y la **DB** son
  privados/propietarios. No los subas al repo público. El trabajo de cliente
  puede *leer* de ellos, pero el resultado que se commitea a `old-school-openko`
  no debe contener su código ni sus assets.

## Orden de arranque sugerido para la sesión local

1. Leer estos 10 documentos.
2. Clonar/ubicar `old-school-openko/` y compilarlo una vez (confirmar que el
   entorno está listo: CMake, SDL3, FreeType, etc.).
3. Fase 0 del servidor (documento 04): informe de evaluación de `game-server/`.
4. Fase 0 del cliente (documento 05): diff de cifrado y protocolo leyendo
   `game-server/`.
5. Con esos dos informes, decidir el orden real de ataque (servidor primero para
   tener contra qué probar, o cifrado del cliente primero).

## Recordatorio final

Estos documentos son el mapa que dejó la sesión anterior. Si el código
contradice al documento, **gana el código** — y actualiza el documento.
