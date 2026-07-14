# 07 — Base de datos

## Qué es

La DB 24xx completa (MSSQL): tablas, procedimientos almacenados, etc. Contiene
la definición del contenido del juego (ítems, skills, monstruos, precios,
drops, eventos) y el estado (cuentas, personajes). El servidor (`Login` /
`GameServer`) se conecta a ella.

Actualmente corre en un servidor remoto. Hay credenciales de acceso.

## SEGURIDAD DE CREDENCIALES — leer antes de conectar

**Nunca escribas el usuario/contraseña de producción en el chat, en un prompt,
ni en un archivo versionado.** Quedan en el historial en texto plano.

Patrón correcto:

1. Guarda las credenciales en un archivo **`.env` local**, y asegúrate de que
   `.env` esté en `.gitignore` (en `old-school-openko/` ya lo está; verifica en
   `game-server/`).
2. El código lee las credenciales de **variables de entorno**, no las lleva
   hardcodeadas.
3. Al pedirle algo al agente, di *"las credenciales están en `.env`, léelas de
   ahí"* — nunca las pegues.

### Recomendación fuerte para desarrollo

En vez de trabajar contra la DB de producción:

- Restaura un **backup** (`.bak`) de la DB en un **MSSQL local**. Lo más simple
  es Docker:

  ```
  docker run -e "ACCEPT_EULA=Y" -e "MSSQL_SA_PASSWORD=<pass-local-de-dev>" \
    -p 1433:1433 -d mcr.microsoft.com/mssql/server:2022-latest
  ```

  Luego restaurar el `.bak` con `sqlcmd` / `RESTORE DATABASE`.
- Ventaja: puedes romper, migrar y experimentar sin miedo, y no expones
  producción.

## Conexión desde el servidor (nanodbc)

El servidor 12xx open-source usa **nanodbc** para hablar ODBC en Linux (en vez
del ODBC de Windows). El port del servidor 24xx (documento 04) debe seguir el
mismo patrón:

- Driver ODBC de MSSQL para Linux (`msodbcsql18` o similar) + `unixODBC`.
- nanodbc como wrapper C++.
- Mira cómo `old-school-openko/src/Server/` configura la conexión (cadena de
  conexión, DSN o connection string directa).

## Uso de la DB en este proyecto

- **Referencia de contenido**: la DB dice qué ítems/skills/etc. existen en 24xx
  y con qué IDs/stats. Sirve para saber qué debe cargar el cliente (documento
  06, Fases 2 y 3).
- **Procedimientos almacenados**: buena parte de la lógica de servidor de KO
  vive en SPs. Al portar el servidor, revisar que los SPs existan en la DB y que
  el driver Linux los pueda invocar igual.

## Advertencia

No corras migraciones ni `ALTER`/`DROP` contra la DB de producción durante el
desarrollo. Trabaja siempre sobre la copia local restaurada.
