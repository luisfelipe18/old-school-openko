# Contexto del proyecto — Actualización del cliente Knight Online 12xx → 24xx

> **Esta rama (`feature/contextos`) contiene SOLO documentación.** Es un paquete
> de handoff para una nueva sesión de Claude Code **local** que continuará el
> desarrollo. No hay código aquí a propósito: el código vive en las carpetas
> hermanas descritas abajo.

## Para el agente que lee esto

Eres una nueva sesión de Claude Code trabajando en local. No tienes memoria de
las sesiones anteriores. Estos documentos te transfieren todo el contexto que
necesitas. **Léelos en orden antes de tocar nada.**

## Orden de lectura

| # | Documento | Qué cubre |
|---|-----------|-----------|
| 01 | [`01-OBJETIVO-Y-ALCANCE.md`](01-OBJETIVO-Y-ALCANCE.md) | Qué se quiere lograr y por qué |
| 02 | [`02-ESTRUCTURA-DE-CARPETAS.md`](02-ESTRUCTURA-DE-CARPETAS.md) | Las 4 carpetas de trabajo y qué hay en cada una |
| 03 | [`03-CONTEXTO-OLD-SCHOOL-OPENKO.md`](03-CONTEXTO-OLD-SCHOOL-OPENKO.md) | El port open-source ya existente (base de todo) |
| 04 | [`04-PLAN-PORT-SERVIDOR-LINUX.md`](04-PLAN-PORT-SERVIDOR-LINUX.md) | Portar el servidor 24xx a Linux reusando lo ya hecho |
| 05 | [`05-PROTOCOLO-Y-CIFRADO.md`](05-PROTOCOLO-Y-CIFRADO.md) | La capa de red y el cifrado — el corazón del asunto |
| 06 | [`06-PLAN-ACTUALIZACION-CLIENTE.md`](06-PLAN-ACTUALIZACION-CLIENTE.md) | Actualizar el cliente 12xx al protocolo/contenido 24xx |
| 07 | [`07-BASE-DE-DATOS.md`](07-BASE-DE-DATOS.md) | Manejo de la DB y seguridad de credenciales |
| 08 | [`08-ANTICHEAT.md`](08-ANTICHEAT.md) | Por qué el anticheat NO forma parte de este trabajo |
| 09 | [`09-METODOLOGIA-Y-CONVENCIONES.md`](09-METODOLOGIA-Y-CONVENCIONES.md) | Cómo trabajar, verificar y no romper nada |

## Resumen en una frase

Existe un port open-source del cliente **12xx** de Knight Online a macOS/Linux
(carpeta `old-school-openko/`). Se quiere **actualizarlo para que hable con un
servidor 24xx** (código fuente en `game-server/`, contenido/assets en
`client/`), aprovechando que ya se resolvió lo más difícil del port de
plataforma. El primer bloqueo técnico real es el **cifrado de red**; el arte y
el contenido nuevo NO son un problema porque `client/` ya los tiene.

## Nota sobre estos documentos

Se generaron en una sesión previa (Claude Code en la nube) que trabajó durante
semanas en el port 12xx. Reflejan el estado y las decisiones de ese trabajo.
Cuando empieces, **verifica contra el código real** — los documentos son el
mapa, no el territorio; si algo no coincide, gana el código.
