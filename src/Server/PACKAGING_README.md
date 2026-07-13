OpenKO — server daemons (Knight OnLine)
=======================================

This package contains the five server daemons that make up an OpenKO shard,
plus the MAP and QUESTS data they read. They are meant to run together on one
host (or a private network), in this same folder.

Contents
--------
  Ebenezer          the game/world server (players connect here)
  AIServer          drives NPCs / monsters
  Aujard            database proxy (talks to the game DB)
  ItemManager       item logic service
  VersionManager    client version / patch-list service
  MAP/              zone geometry the world/AI servers load
  QUESTS/           quest scripts Ebenezer loads

What you must provide yourself
------------------------------
  * The game DATABASE. The servers reach it through ODBC. Install a driver and
    create the DSNs the servers expect:
      Linux:  sudo apt install unixodbc odbc-<your-driver>   (e.g. FreeTDS/MSSQL)
      macOS:  brew install unixodbc <your-driver>
    Configure the DSN in ~/.odbc.ini (or /etc/odbc.ini) with the DB host,
    credentials and database name your schema uses.
  * Any per-server *.ini configuration (ports, peer IPs, DSN names). The
    servers read MAP/QUESTS from this folder by default; override with
    --map-dir / --quests-dir if you move them.

Runtime system libraries
------------------------
  unixODBC (libodbc). Everything else the daemons need is statically linked.

Suggested start order
---------------------
  1. VersionManager     (client version + server list)
  2. Aujard             (DB proxy - must reach the database)
  3. ItemManager
  4. AIServer
  5. Ebenezer           (last; players log in here)

Each is a console daemon; run them from this directory, e.g.:
  ./Aujard &
  ./AIServer &
  ./Ebenezer

Building this package yourself
------------------------------
  sudo apt install unixodbc-dev
  cmake --preset linux-clang-release
  cmake --build build/linux-clang-release --target \
    Ebenezer AIServer Aujard ItemManager VersionManager
  cpack --config build/linux-clang-release/CPackConfig.cmake \
    -G TGZ -D CPACK_COMPONENTS_ALL=Server
