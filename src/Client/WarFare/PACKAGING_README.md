OpenKO — WarFare client (Knight OnLine) for macOS / Linux
=========================================================

This package contains ONLY the open-source client. It does NOT include the
Knight OnLine game data (Data/, UI/, textures, Server.Ini, ...), which is
proprietary content you must supply yourself.

Contents (Linux tarball)
-------------------------
  bin/KnightOnLine          the game client
  bin/libopenal.so.1        vendored audio library (not on most systems)
  bin/libjpeg.so.9          vendored JPEG library (distinct from the system's)
  bin/*.cur                 mouse cursors
  bin/openko-client.desktop optional desktop-menu entry (copy to
                            ~/.local/share/applications/ to integrate)

Running (Linux)
---------------
  1. Put your game data where the client can find it. Any of these works:
       - a "GameData" folder next to the binary:   bin/GameData/
       - ~/GameData/
       - launch the binary from inside your data directory
       - pass it explicitly:  ./bin/KnightOnLine --data /path/to/gamedata
  2. Run it:
       cd bin && ./KnightOnLine
     (The binary finds its bundled .so files via an $ORIGIN rpath; no
     LD_LIBRARY_PATH needed.)

Runtime system libraries (Linux)
--------------------------------
These are expected to be already installed (they are on a normal desktop):
  libfreetype6, libpng16, libbz2, libbrotli, libGL/Mesa, X11/Wayland libs.
On Debian/Ubuntu:  sudo apt install libfreetype6 libpng16-16 libgl1

Running (macOS)
---------------
  Drag KnightOnLine.app to /Applications. The .app is self-contained (its
  dylibs are bundled inside). Place your game data in
  ~/Library/Application Support/OpenKO/GameData/  (or pass --data).

Signing & notarization (macOS, distributor step)
-------------------------------------------------
An unsigned .app triggers Gatekeeper. To distribute it, sign and notarize with
your own Apple Developer account:

  codesign --deep --force --options runtime --timestamp \
    --sign "Developer ID Application: <Your Name> (<TEAMID>)" \
    KnightOnLine.app

  # staple after notarizing the containing .dmg:
  xcrun notarytool submit OpenKO-Client-*.dmg \
    --apple-id <you@example.com> --team-id <TEAMID> --wait
  xcrun stapler staple KnightOnLine.app

Building the packages yourself
------------------------------
  cmake --preset linux-clang-release        # or macos-arm64-release
  cmake --build build/<preset> --target WarFare
  cpack --config build/<preset>/CPackConfig.cmake -G TGZ    # or DragNDrop
