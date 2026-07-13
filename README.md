> [!IMPORTANT]
> **This project is strictly for academic purposes only.**
> 
> This project **cannot currently be used for a real server**. It is still in a very early developmental state.
> The server and client are both lacking full (or any) support in numerous critical features; upgrades, wars, even basic exchanges, etc, let alone things like the Power-Up Store.

Dont forget to run `git submodule update --init --recursive`


# Open Knight Online (OpenKO)

We started this project to learn more about how the MMORPG Knight Online works. MMORPGs are very intricate programs requiring knowledge in many areas of computer science such as TCP/IP, SQL server, performance tuning, 3D graphics and animation, load balancing, etc. Starting with the original leaked source, we have updated to DirectX 9, added function flags so that various file formats may be supported while remaining backwards compatible, and much, much more.

If you have questions, or would like help getting started, feel free visit [our Discord](https://discord.gg/Uy73SMMjWS).

### Project Setup

Currently, OpenKO supports 2 separate sets of builds:
1. Windows (Visual Studio/MSBuild): Server, Client, Tools (preferred for overall development) 
2. Cross-platform (CMake): Server only at this time

A guide to setting up and building this project is maintained on the wiki: 
* [Windows Project Setup](https://github.com/Open-KO/KnightOnline/wiki/Project-Setup-(Windows))
* [Linux Project Setup](https://github.com/Open-KO/KnightOnline/wiki/Project-Setup-(Linux))

The following setups are tested by our GitHub workflows and are known to build:
 - Windows 11 **(all projects: client, server, tools, etc)**
   - Microsoft Visual Studio 2022 (v143)
   - Microsoft Visual Studio 2026 (v145) (this is not yet tested by our workflows - it will be when GitHub updates their runners - but is routinely used in local development)
 - Ubuntu 24.04 **(only the server projects at this time)**
   - clang 18 (current), 20 (bleeding edge)
   - gcc 13 (current)
 - macOS 15 **(only the server projects at this time)**
   - Apple Clang 15

#### POSIX (macOS/Linux) client port

The full client, plus the ported client tools (`Option`, `Launcher`), builds and
runs on macOS/Linux; the roadmap and status live in
[docs/PORT_POSIX_PLAN.md](docs/PORT_POSIX_PLAN.md). Configuration is opt-in via
`-DOPENKO_CLIENT_POSIX_EXPERIMENTAL=ON` / `-DOPENKO_CLIENT_TOOLS_POSIX_EXPERIMENTAL=ON`,
both preset in `CMakePresets.json`. The whole thing builds in three commands:

```
cmake --preset macos-arm64-release        # or linux-clang-release, *-debug, ...
cmake --build --preset macos-arm64-release
ctest --preset macos-arm64-debug          # test suites (debug presets)
```

`cmake --build` without `--target` builds everything configured: the game
(`KnightOnLine`), the tools, and every test suite. The server projects need a
system ODBC library (`apt: unixodbc-dev` / `brew: unixodbc`) and are skipped
with a notice when it's missing. The `Client POSIX` CI jobs build all of this
on Linux and macOS in Debug and Release on every push.

Suggested packages:
* macOS: `brew install cmake ninja sdl3 freetype`
* Ubuntu/Debian: `apt install build-essential clang cmake ninja-build libfreetype-dev`
  (SDL3 is fetched and built from source automatically when not installed system-wide;
  FreeType uses the system package first, then falls back to the source build)

##### Render backends

The client renders through an RHI with three POSIX backends, selected by
`Renderer=` under `[Screen]` in `Option.ini` or the `--renderer` CLI flag:

* `GL` - OpenGL 3.3 core. The default everywhere for now.
* `SDLGPU` - SDL_GPU: **Metal** on macOS, **Vulkan** on Linux. Fully validated
  on Vulkan; on Metal the in-world workload currently shows rendering
  corruption (under investigation), so it stays opt-in. Falls back to GL
  automatically when no Metal/Vulkan driver exists.
* `Null` - headless, for CI smoke runs.

##### Sanitizer build (docs/PORT_POSIX_PLAN.md, F9)

The `linux-asan` preset builds the client subset with
`-fsanitize=address,undefined` for the stability pass:

```
cmake --preset linux-asan
cmake --build build/linux-asan
ctest --preset linux-asan
```

It defaults to gcc (whose sanitizer runtime ships with `g++`); pass
`-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` if your clang has
`libclang_rt.asan` installed. Run the client smoke under the sanitizers
with `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./KnightOnLine --smoke 30`.

##### Packaging (docs/PORT_POSIX_PLAN.md, F8)

After a successful build, `cmake --install <build-dir> --prefix <dest>`
produces a distribution layout:

* **Linux**: `bin/KnightOnLine` + the `.cur` cursor files as siblings +
  `share/applications/openko-client.desktop`. The binary's rpath is
  `$ORIGIN`, so vendored dependency shared libraries can ship in `bin/`
  as well.
* **macOS**: a `Knight OnLine.app` bundle. Info.plist ships with
  `NSHighResolutionCapable`, category role-playing, and a minimum system
  version of macOS 11. The `.cur` cursors live in
  `Contents/Resources/`. For the app icon, drop a square PNG (ideally
  1024x1024) at `src/Client/WarFare/AppIcon.png` - the build crops it and
  generates a proper multi-resolution `KnightOnLine.icns` automatically
  (`generate_macos_icon.sh`, via `sips`+`iconutil`). A committed
  `KnightOnLine.icns` takes precedence if you'd rather hand-author one; a
  low-res fallback from `WarFare_1298.ico` is used if neither exists. To
  run an unsigned bundle locally, `codesign --force --deep -s - Knight\
  OnLine.app` gives it an ad-hoc signature and Gatekeeper won't
  quarantine it.

  If the Dock/Get Info preview shows the new icon but Finder's list/icon
  view still shows a generic one after rebuilding, that's Finder's icon
  cache being stale (common after overwriting a `.app` in place) - not a
  build problem. Force a refresh with:
  ```
  touch "Knight OnLine.app"
  killall Finder Dock
  ```
  or, if that doesn't clear it, reset the whole icon cache:
  ```
  sudo rm -rf /Library/Caches/com.apple.iconservices.store
  killall Finder Dock
  ```

Runtime data written by the game (`Log.txt`) lands under
`~/Library/Application Support/OpenKO/` on macOS and
`$XDG_CONFIG_HOME/openko` (or `~/.config/openko/`) on Linux, so a
read-only install location is fine.

##### Pointing the client at a game-data directory

The client reads `Server.Ini`, `Data/`, `UI/` and the rest of the game
assets from a "game data directory". The build system stages whatever
you drop into `<repo>/assets/Client/` (Server.Ini, Data/, UI/, textures,
...) into `GameData/` next to the built binary on Linux, or into
`Contents/Resources/GameData/` inside the `.app` bundle on macOS - so a
build produces a self-contained runtime out of the box. `assets/Client/`
starts empty; drop your own client files there.

If you'd rather point at a different directory, you can, in this
precedence order:

1. `./KnightOnLine --data /path/to/game-data ...` (explicit override).
2. `OPENKO_GAME_DATA=/path/to/game-data ./KnightOnLine ...` (env var).
3. Otherwise the client auto-discovers by looking, in order, at: the
   current working directory, `<exe-dir>/GameData/`, the exe directory
   itself, `Contents/Resources/GameData/` and `Contents/Resources/`
   inside a macOS bundle, the bundle's parent, `~/GameData`, and
   `~/Library/Application Support/OpenKO/GameData` (macOS) or
   `~/.local/share/openko/GameData` (Linux). The first entry that
   contains `Data/` or `Server.Ini` wins.

The same `--data`/`OPENKO_GAME_DATA`/auto-discovery rules apply to the
POSIX client tool ports (`Option`, ...; see below) via
`Platform/GameDataDir.h`, so pointing one at your install points all of
them at it.

##### Boot mode

The client boots straight into the login scene by default (the classic
Knight OnLine presentation menu). CLI flags opt out for CI/dev:

* `--diagnostics` runs a clear-color loop with input diagnostics.
* `--test-scene` draws the RHI diagnostic scene (rotating triangle +
  textured quad).
* `--smoke <N>` pumps N frames headless and exits (implies
  `--diagnostics`); this is what CI runs.
* `--scene login` stays valid for backwards compatibility, but it's now
  the default so passing it is redundant.

### Visual Studio solutions

Solutions are available in the root directory:
* `All.slnx` - all of the various projects.
* `Client.slnx` - just the client project (WarFare) and its dependencies.
* `ClientTools.slnx` - just the client tool projects (KscViewer, Launcher, Option) and their dependencies.
* `Server.slnx` - just the server projects (AIServer, Aujard, Ebenezer, ItemManager, VersionManager) and their dependencies.
* `Tests.slnx` - our various unit test projects.
* `Tools.slnx` - just the tool projects (N3CE, N3FXE, N3ME, N3TexViewer, N3Viewer, SkyViewer,  TblEditor, UIE) and their dependencies.

## Goals

At this early stage in development, the goal of this project is to replicate official client functionality while preserving accuracy and compatibility with the official client and server.
This allows us to be able to side-by-side the client/server for testing purposes.

We do not intend to introduce features not found in the official client, nor introduce custom behaviour in general. You're very welcome to do so in forks however, but these do not mesh with our design goals and introduce complexity and potentially incompatibility with the official client. Essentially, in the interests of accuracy, we'd like to keep the client's behaviour as close to official as possible, where it makes sense.

We may deviate in some minor aspects where it makes sense to fix, for example, UI behaviour, or to provide the user with error messages where the client may not officially do so, but these changes do not affect compatibility while improving the user experience.

Pull requests for such changes will be accepted on a case-by-case basis.

As a hard-and-fast rule, this means we **DO NOT** change client assets or the network protocol between the client and game server (Ebenezer).

The client **MUST** remain compatible with the official client and the official server.

We do not intend to keep the backend servers compatible with the official ones (e.g. our AI server standing in for the 1.298 AI server and vice versa).
This is largely more work than really necessary to maintain. Side-by-side testing with the server files as a collective is more than sufficient here.

Late in development when side-by-side development is rarely necessary, it will make sense to start deviating from official behaviour for improvements and custom features.
At such time, we will welcome such changes, but doing so this early just creates incompatibilities (making it harder to test them side-by-side) and unnecessarily diverts
attention when there's so much official behaviour/features still to implement, update and fix.

## Intentional design decisions

* _The project is currently focused around supporting the 1298/9 version of the game_. Version 1298/9 has most of the core functionality attributed to the game’s success. By ignoring later versions of the game we keep the system relatively simplistic. This allows us to strengthen the fundamental components of the game while minimizing the amount of reverse engineering necessary to make things work.

* _We stick to the 1298/9 database schema_. To ensure compatibility with the 1298/9 version of the game we do not modify the basic database schema. This means the structure of the database and how information is stored in the database doesn’t change while we are working. This could change once the core functionality of the 1298/9 is in place.

## Contributing

Considering contributing? Great! That's what this project's for.
We'll typically accept most PRs, but please be aware of a few ground rules:

### It must respect our project goals
The PR must respect our goals as outlined above. That is, no asset changes, features must exist in 1.298 and behave correctly in both the official client & server, etc.

### It must be made / written by you
The change must have been made / written by you. If contributing on behalf of someone else, then this should be acknowledged.

### No AI use
This is somewhat an extension of the previous rule (as the code must be your own), but as enticing as AI is for "convenience", and how much it is pushed in virtually all areas of our day-to-day life, it is a poor replacement for a human's own critical thinking.
The purpose of this project is for academic/learning purposes. You should not be trying to learn from AI.

At absolute best, it is acceptable to use it to (after you've done so yourself already) verify your work and try to spot any issues. But even here, anything it says should be taken with a huge grain of salt.
AI is highly inaccurate, makes things up on a whim (i.e. hallucinates) and is confident enough to persist in telling you that it's not until you push back (and even then, it may still not budge).

Regarding the code quality itself, it will regurgitate patterns it's seen, good or bad (mostly bad). The code may not even address what you're intending it to address.

We expect a certain level of code quality in our codebase (which we're happy to work with you to help improve your code before we merge it into our codebase -- **if you wrote it yourself**).
AI, however, is a waste of everybody's time.

AI usage is reasonably easy for us to detect.

Please do not use AI. Thank you.

<p align="center">
	<img src="https://github.com/Open-KO/KnightOnline/blob/master/openko_example.png?raw=true" />
</p>
