# Guía de tareas del port POSIX — asignación por agente/modelo

> Complemento de `PORT_POSIX_PLAN.md` (roadmap con tareas `T*` y criterios de
> aceptación) y `PORT_POSIX_CONTEXT.md` (reglas, gotchas, build/test — **de
> lectura obligatoria antes de cualquier tarea**).
>
> La columna "Modelo alternativo" indica qué modelo de Claude (y con qué
> esfuerzo de razonamiento) puede resolver la tarea cuando Claude Fable 5 no
> esté disponible. Regla general: las tareas mecánicas van a Sonnet/Haiku;
> las que tocan la interfaz RHI o fidelidad visual, a Opus (o Fable).

## Prompt de arranque sugerido para cualquier agente

> Lee `docs/PORT_POSIX_CONTEXT.md` completo y luego la tarea TX.Y en
> `docs/PORT_POSIX_PLAN.md`. Trabaja en la rama `feature/port-posix`, deja la
> rama verde (build completo + `ctest` + smoke `--smoke 30`) y marca la
> checkbox de la tarea en el plan antes de pushear.

## Tabla de tareas

| # | Tarea | Qué es | Dificultad | Modelo alternativo (esfuerzo) |
|---|---|---|---|---|
| T6.1 | Buffers RHI (Lock/Unlock + Null/D3D9) y migrar terreno/clima/UIImage | Diseño de interfaz pequeño + migración mecánica | Media-alta | Opus 4.8 (high) — toca la firma de la RHI |
| T6.2 | Texturas RHI + port de `N3Texture` (carga DXT headless) | La más delicada de F6: tamaños de bloque DXT, layout por nivel | Alta | Opus 4.8 (high) |
| T6.3 | Rezagados: `ValidateDevice`/`SetScissorRect` a la RHI, gate de `N3Cloak` | Mecánica pura con patrón ya establecido | Baja | Sonnet 5 (medium); Haiku 4.5 (high) también sirve |
| T6.4 | Strings `IDS_*` portables (tabla en vez de `.rc`) | Mecánica repetitiva, muchas cadenas, riesgo bajo | Baja-media | Sonnet 5 (medium) |
| T6.5 | `RHIDeviceGL` a: contexto GL + glad + clear/present | Bring-up gráfico acotado | Media | Sonnet 5 (high) u Opus 4.8 (medium) |
| T6.6 | GL b: VBO/IBO, ring-buffer UP, texturas DXT/BGRA | Detalles de GL con trampas (flip V, BGRA) | Media-alta | Opus 4.8 (medium-high) |
| T6.7 | GL c: über-shader fixed-function (stages, fog, luces) | **La más difícil del port** — fidelidad visual | Muy alta | Opus 4.8 (high) — reservar para Fable si es posible |
| T6.8 | Conectar `CGameProcedure` al main SDL → **hito C: login en macOS** | Compile-fix iterativo grande + integración | Alta | Opus 4.8 (high); el barrido de gates previo: Sonnet 5 (high) |
| T7.1 | `DFont` sobre FreeType (atlas de glifos) | Bien especificada, dominio conocido | Media-alta | Opus 4.8 (medium) o Sonnet 5 (high) |
| T7.2 | `N3UIEdit` con SDL text input/IME | Integración con matices (IME coreano) | Media | Sonnet 5 (high) |
| T7.3 | Encoding CP949 en fronteras de chat | Mecánica; los helpers ya existen y están testeados | Baja | Haiku 4.5 (high) o Sonnet 5 (low) |
| T6b.1-3 | Backend SDL_GPU (shaders offline + caché de pipelines + paridad) | Arquitectura nueva sobre base ya diseñada | Alta | Opus 4.8 (high) |
| F8 | Empaquetado `.app`/`.desktop`/icns | CMake + convenciones de SO, sin riesgo de motor | Baja | Sonnet 5 (medium) |
| — | Mantenimiento: sync vcxproj, checklists del plan, CI | Trivial con el doc de contexto | Muy baja | Haiku 4.5 (medium) |

## Consejos operativos

1. **Orden por costo:** T6.3, T6.4 y T7.3 son baratas y desbloquean a las
   caras — asignarlas primero a Sonnet/Haiku. Reservar Fable/Opus-high para
   T6.2, T6.7 y T6.8, donde un error cuesta caro.
2. **Una tarea por sesión, secuencial sobre `feature/port-posix`.** Dos
   agentes en paralelo sobre la misma rama se pisan los commits. Para
   paralelizar, dar a cada agente una rama hija (`feature/port-posix-t6.4`)
   y mergear manualmente.
3. **Validación externa periódica:** tras cada tarea gorda, compilar y correr
   los tests en el Mac (`cmake --preset macos-arm64-debug`), y mantener un PR
   draft hacia `master` para que la CI de Windows valide el código compartido.
