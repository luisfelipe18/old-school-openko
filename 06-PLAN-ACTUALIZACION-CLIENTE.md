# 06 — Plan: actualizar el cliente 12xx → 24xx

El objetivo final. Se hace **dentro de `old-school-openko/`**, por incrementos,
apoyándose en el diff de protocolo/cifrado (documento 05) y en los assets 24xx
(`client/`).

## Principio rector

**Incremental y siempre compilando.** Igual que el port de plataforma original:
cada fase deja el cliente en un estado compilable y verificable (build + ctest +
smoke). No romper Windows ni el camino 12xx mientras se avanza.

> Estrategia de ramas sugerida: trabajar en una rama tipo `feature/upgrade-24xx`
> partiendo de `master` (o de `feature/add-shinning` si se quiere heredar el
> brillo/HiDPI/fuentes/toggle). Decidir al empezar.

## Fase 1 — Conexión (el hito "conecta o no conecta")

Lo mínimo para que el login **complete** contra el servidor 24xx:

1. **Cifrado**: portar el esquema de `game-server/` al cliente (reemplazar/
   ajustar `JvCryption`). Documento 05.
2. **Handshake / version check**: adaptar el flujo inicial (el 12xx recibe una
   clave de cifrado en el version check — ver `GameProcLogIn_1298.cpp`).
3. **Opcodes de login**: adaptar los `LS_*` que cambien.

Éxito = el cliente open-source conecta al `Login` 24xx, pasa el handshake, y
recibe la lista de servidores / entra al `GameServer`.

## Fase 2 — Tablas de datos (`.tbl`)

El contenido (ítems, skills, etc.) vive en tablas binarias que el cliente lee
(`Item_Org`, `Item_Ext_N`, `skill_magic_main`, `Zones`, etc., cargadas en
`CGameBase::StaticMemberInit`). En 24xx estas tablas tienen más columnas / otro
layout.

- Comparar el formato de las tablas de `client/` (24xx) con lo que leen los
  structs `__TABLE_*` del 12xx (`GameDef.h`).
- Actualizar los structs y los lectores para el formato 24xx.
- La **DB 24xx** y el servidor son la referencia de qué campos existen.

## Fase 3 — Assets (modelos, íconos, UI, FX)

Los assets 24xx están en `client/`. El motor Noah3D lee `.dxt`, `.n3chr`,
`.n3cplug`, `.n3cpart`, `.ksc`, `.uif`, etc. Si el formato subió de versión:

- Inspeccionar con las herramientas ya portadas: **AssetExplorer** y
  **KscViewer** (en `old-school-openko/src/Client/`).
- Actualizar los `Load()` de los formatos que hayan cambiado (versión de
  cabecera, campos nuevos).
- Cablear el contenido nuevo: que un ítem/skill/monstruo 24xx encuentre su
  modelo/ícono/FX en `client/`.

## Fase 4+ — Sistemas nuevos, feature por feature

Cada sistema que 24xx agregó y 12xx no tiene, como su propio incremento:

- Nuevos niveles / clases / árboles de skills.
- Nuevos slots de equipamiento, tipos de arma/armadura.
- Eventos, Power-Up Store, y demás UIs nuevas.
- Cada uno: leer el servidor (qué paquetes/lógica), traer el asset de `client/`,
  implementar en el cliente, validar contra el servidor 24xx.

## Orden de prioridad recomendado

1. **Conexión** (Fase 1) — sin esto no hay nada que probar.
2. **Login → selección de personaje → entrada al mundo** — el flujo que ya
   funciona en 12xx, pero ahora contra el servidor 24xx (paquetes actualizados).
3. **Render del personaje propio con equipo 24xx** (tablas + assets).
4. **Movimiento, chat, mundo básico**.
5. **Sistemas nuevos** por demanda.

## Qué reusar del trabajo reciente

La rama `feature/add-shinning` ya tiene cosas que probablemente quieras heredar:
brillo de ítems upgradeados, fuentes nítidas (variante E), HiDPI/Retina, y el
toggle de login. Ver documento 03. Considera basar la rama de upgrade sobre ella.

## Recordatorio de verificación

Cada incremento: `cmake --build ...` + `ctest` + `--smoke 30`, y cuando aplique,
prueba real login→mundo contra el servidor 24xx. Ver documento 09.
