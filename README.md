
<img width="848" height="204" alt="banner" src="https://github.com/user-attachments/assets/2a0af91e-a15e-462f-aa99-d2869a311675" />

# COLD START  `v0.9.9`

COLD START is a top-down action shooter built in C++ with SDL2 for PC and Nintendo Switch homebrew. It combines fast combat, local content editing, multiplayer support, and a lightweight modding pipeline aimed at rapid iteration.

## Highlights

- PC and Nintendo Switch builds from the same codebase
- Arena, co-op, deathmatch, team deathmatch, playlist, and custom-map play modes
- New `PLAY` submenu for Generated Map / Map / Pack selection
- Host-authoritative online multiplayer with ENet-based state sync
- Procedural and custom maps, map packs, character content, and upgrade crates
- In-game map editor and built-in pixel-art texture editor
- Mod loading with support for maps, packs, characters, items, gamemodes, sprite overrides, sound overrides, and synced multiplayer mod payloads

### Some things do not work, im actively working on this :)

## Screenshots


![2026030516054100-FC1A835C590C06D4071528FDF96C9516](https://github.com/user-attachments/assets/dff2c7bc-5693-4395-b25a-260d1d97414a)
![2026030522145200-99584FB39514334B3ED85316B5F4C344](https://github.com/user-attachments/assets/e466ca9d-418d-4829-acb1-30453b330efd)
![2026030522150100-99584FB39514334B3ED85316B5F4C344](https://github.com/user-attachments/assets/1b99b04d-2426-407f-b596-bbc2cccf13f5)
![2026030522152200-99584FB39514334B3ED85316B5F4C344](https://github.com/user-attachments/assets/63370a2a-55ec-4138-a777-9818b5b4d5f4)



## Feature Set

### Gameplay

- Fast top-down shooter combat with melee and ranged enemies
- Dash-capable melee enemies, projectile enemies, bombs, parry, pickups, and upgrade crates
- Generated-map runs can now be configured directly from the `PLAY` menu
- PvE and PvP rule sets with configurable lives, friendly fire, teams, wave count, spawn scaling, and crate intervals
- Spectating, respawning, score tracking, and lobby flow for multiplayer sessions

### Tools

- In-game map editor
- Character and content pipeline for custom assets
- Built-in texture editor for tiles, sprites, and UI art

### Multiplayer

- Host/join flow over ENet
- Lobby configuration sync
- Player state, projectile, explosion, pickup, enemy, and event replication
- Support for large lobbies, with host max player configuration up to 128
- Mod synchronization path for lightweight multiplayer content distribution

## Requirements

### PC

- CMake 3.14+
- C++17 compiler
- SDL2
- SDL2_image
- SDL2_ttf
- SDL2_mixer
- pkg-config

### Nintendo Switch

- devkitPro
- libnx
- SDL2, SDL2_image, SDL2_ttf, SDL2_mixer for Switch

## Build

### PC

```bash
cd cold_start
mkdir -p build-pc
cd build-pc
cmake ..
make -j4
```

Binary: `build-pc/cold_start`

### Windows

```bash
cd cold_start
mkdir -p build-win
cd build-win
cmake ..
make -j4
```

Binary: `build-win/cold_start.exe`

### Nintendo Switch

```bash
cd cold_start
make -j4
```

Binary: `cold_start.nro`

## Run

### PC

```bash
cd cold_start/build-pc
./cold_start
```

### Dedicated server (Linux / DigitalOcean)

```bash
cd cold_start/build-pc
./cold_start --dedicated --port 7777 --max-players 16 --name do-cold-start
```

Useful flags:

- `--password <value>` set lobby password
- `--name <value>` server display name shown in lobby
- `--max-players <2..128>` host lobby capacity

DigitalOcean quick setup:

```bash
cd cold_start
chmod +x deploy/digitalocean/install_server.sh
./deploy/digitalocean/install_server.sh
sudo cp deploy/digitalocean/cold_start.service /etc/systemd/system/cold_start.service
sudo systemctl daemon-reload
sudo systemctl enable --now cold_start
sudo ufw allow 7777/udp
```

### Windows

Run `build-win/cold_start.exe`

### Switch via nxlink

```bash
cd cold_start
nxlink -a <SWITCH_IP> -s cold_start.nro
```

## Release artifacts

Current manual release artifacts for `v0.9.1`:

- `cold_start-0.9.1-linux.zip`
- `cold_start-0.9.1-windows.zip`
- `cold_start.nro`

## Controls

### Keyboard and mouse

- Move: `WASD`
- Aim: mouse
- Shoot: left mouse button
- Parry: `Space`
- Bomb: `Q`
- Pause: `Esc`

### Gamepad

- Move: left stick
- Aim: right stick
- Shoot: RT
- Parry: LB
- Bomb: LT
- Pause: Start

## Project Layout

- `source/` — gameplay, networking, editors, modding, and core systems
- `romfs/` — bundled runtime assets
- `maps/` — standalone maps
- `characters/` — character definitions
- `tiles/` — tile content
- `build-pc/` — CMake build output for desktop
- `build-win/` — CMake build output for Windows cross-builds

## Main menu flow

- `PLAY` → choose `GENERATED MAP`, `MAP`, or `PACK`
- Generated-map settings now live in the `PLAY` submenu:
	- Map Width
	- Map Height
	- Player HP
	- Enemy Spawnrate
	- Enemy HP
	- Enemy Speed
- Standalone `MAPS` / `PACKS` shortcuts remain available from the main menu

## Modding

Mods are folder-based and discovered from `mods/` and runtime mod directories.

### Supported content

- Characters: `.cschar`
- Maps: `.csm`
- Packs: `.cspack`
- Sprite overrides and additional sprites
- Sound overrides
- Custom items
- Custom gamemodes
- Numeric/string gameplay overrides

### Folder structure

```text
mods/
	mymod/
		mod.cfg
		characters/
		maps/
		packs/
		sprites/
		sounds/
		gamemodes/
		items/
```

### `mod.cfg` example

```ini
[mod]
id=mymod
name=My Mod
author=Author
version=1.0
description=Example content pack
game_version=1

[content]
characters=true
maps=true
packs=true
sprites=true
sounds=true
gamemodes=true
items=true

[overrides]
player_speed=600
enemy_hp=5

[dependencies]
dep1=baseexpansion
```

### Content notes

- `characters/` is scanned for `.cschar` files
- `maps/` is scanned for `.csm` files
- `packs/` is scanned for `.cspack` files
- `sprites/` and `sounds/` can override base assets by matching relative runtime paths
- `items/` accepts `.cfg` item definitions
- `gamemodes/` accepts `.cfg` gamemode definitions and registers them into the gamemode registry

### Custom gamemode definition

```ini
[gamemode]
id=survival_plus
name=Survival Plus
description=Harder co-op survival
max_players=8
friendly_fire=false
lives=0
pvp=false
spawn_enemies=true
spawn_crates=true
time_limit=0
respawn_time=5
```

### Multiplayer mod sync

When the host enables mods, lightweight mod data can be serialized and sent to joining clients. Large media files are intentionally skipped; gameplay-critical small files are prioritized.

## Notes

- Runtime config is written to `config.txt`
- Mod enable state is stored in `modconfig.cfg`
- First launch may create missing runtime content directories automatically

## Changelog

### v0.9.9 (2026-03-08)
- **Dedicated server host model rework** — dedicated server is no longer counted as a player slot
- **Lobby host ownership on dedicated** — first client to join becomes lobby host; if lobby host disconnects, a new host is assigned from connected players
- **Lobby host transfer** — lobby host can transfer host permissions to another player directly in lobby; admin and start-game actions now follow lobby-host permissions

### v0.9.8 (2026-03-08)
- **Dedicated server mode** — added `--dedicated` headless runtime with CLI flags (`--port`, `--max-players`, `--password`, `--name`) for cloud hosting
- **DigitalOcean deployment support** — added systemd service unit and install helper under `deploy/digitalocean/` with documented setup steps

### v0.9.7 (2026-03-08)
- **Lobby input fix** — pressing input with the same controller used to enter host/join lobby no longer creates an unintended local sub-player slot; that controller now behaves as the lobby primary controller for confirm/ready actions

### v0.9.6 (2026-03-08)
- **Join menu address input update** — IP CONNECT address field now accepts hostname text (letters, numbers, dots, dashes, and `:` for host:port style endpoints) with increased input length for real-world server addresses

### v0.9.5 (2026-03-08)
- **Splitscreen in multiplayer** — local split-screen now works inside network games; host or join a lobby with multiple gamepads and each local player gets an independent viewport during gameplay; sub-player positions are synced to remote clients via a new `SubPlayerState` network packet so everyone sees all players
- **New network protocol** — added `SubPlayerState` (0x5B) unreliable packet type for streaming local sub-player positions/animation to remote peers at 60 Hz with interpolation
- **Multiplayer splitscreen renderer** — dedicated `renderMultiplayerSplitscreen()` with per-viewport world rendering, remote players, local co-op players, HUD, crosshairs, and minimap with all player dots (local, remote, and remote sub-players)
- **Remote sub-player rendering** — `renderRemotePlayers()` now draws splitscreen companions from other network clients with distinct color tints

### v0.9.3 (2026-03-08)
- **Multiplayer menu reorganization** — moved Local Co-op entry from Play menu to Multiplayer menu for clearer navigation; Play menu now contains only singleplayer game modes
- **Lobby sub-player display** — multiplayer lobby now shows local split-screen companions under each network player with indented "↳ local-N" labels; gamepad join/leave events sync sub-player counts across the network in real-time

### v0.9.1 (2026-03-07)
- **Fix: clients can't damage host in PvP** — the first PvP collision loop was testing ALL bullets (including remote player bullets) against remote players, consuming client bullets near their own NetPlayer representation before they could reach the host; now only local player bullets are checked against remote players

### v0.9 (2026-03-07)
- **Local multiplayer overhaul** — redesigned co-op as local multiplayer: P1 uses keyboard+mouse, P2-P4 join with gamepads; each player gets a username (P1 = config name, P2+ = "pc-N"), multiplayer-style HUD with name tags, HP bars, and kill/death counters
- **Fix: mouse input in multiplayer** — mouse aiming and fire now always work even when a gamepad is connected; gamepad right stick overrides only when active
- **Fix: dead players taking damage** — enemies and bullets no longer hit dead players, preventing skyrocketing death counts
- **Fix: PvP damage** — PvP mode now correctly enables player-vs-player damage; `isPvp` gamemode flag was not propagating to `pvpEnabled` which gates all PvP collision code
- **Fix: local co-op bugs** — slot 0 damage persistence, enemy AI targeting all co-op players, bullet/leg rendering for all slots, pickups collectable by all players

### v0.7.1 (2026-03-07)
- **Fix: singleplayer bomb refill** — kills were not counting toward the bomb recharge threshold because bullet `ownerId` defaults to `255` (unowned) when offline and the multiplayer kill-credit check compared it against `localPlayerId()` (which is `0`), always yielding false; now singleplayer kills always track, multiplayer still credits the correct killer

### v0.7.0 (2026-03-07)
- **New `PLAY` submenu** — `PLAY` now opens a mode picker with `GENERATED MAP`, `MAP`, and `PACK` instead of immediately starting a generated run
- **Generated-map settings moved** — Map Width / Height were moved out of `CONFIG` into the new `PLAY` submenu; solo difficulty sliders are exposed there as well
- **UI framework rollout** — shared UI helpers (`ui.h` / `ui.cpp`) now drive modernized menu rendering, hint bars, separators, sliders, and mouse-click support across menus
- **Fix: multiplayer bomb credit** — enemy kills made by clients now credit the actual killer's bomb progress instead of incorrectly increasing the host's bomb counter
- **Fix: remote player name tags** — multiplayer usernames are now drawn above HP bars to avoid overlap
- **Fix: pause slider labels** — volume rows no longer render duplicated labels such as `Music: Music: 6%`
- **Windows release path** — Windows desktop builds are produced from `build-win/` as `cold_start.exe`

### v0.6.1 (2026-03-06)
- **Windows build** — cross-compile to a single static `.exe` via MinGW-w64 (`build-win/cold_start.exe`); no runtime DLLs beyond standard Windows system DLLs required
- **GitHub CI Windows job** — replaced vcpkg/MSVC approach with MSYS2 MinGW64; installs SDL2 stack from MSYS2 packages and builds with Ninja; artifacts include the `.exe`, any bundled DLLs, and `romfs/`
- **UPnP `r=2` IGD fix** — port mapping is now attempted even when `UPNP_GetValidIGD` returns 2 ("IGD found but reports not connected"), which is the common case on many consumer routers that respond correctly to `AddPortMapping` despite the misleading status

### v0.6.0 (2026-03-06)
- **Map selector in lobby settings** — host can now cycle through Generated / custom maps (`.csm` files + mod-provided maps) with LEFT/RIGHT on the new "Map:" settings row; selected map is sent to all clients and loaded correctly for both generated and custom-map sessions
- **Mod maps included in lobby map list** — `scanMapFiles()` now runs after mods are initialised and appends all enabled-mod map paths; mod maps appear alongside local `.csm` files in the lobby map selector
- **Map Width / Height disabled for custom maps** — the two dimension rows are greyed out and ignored in the input handler when a custom map is active, preventing confusing mismatches
- **Ceiling tiles in map editor** — painting a palette entry from the "ceiling" category now writes to `map_.ceiling[]` and leaves the floor tile unchanged, matching in-game behaviour (transparent overlay, no hitbox)
- **Undo / Redo in map editor** — 64-level undo stack; Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z on keyboard, L3 / R3 on gamepad; snapshot is taken once per continuous stroke
- **Custom tile textures bundled with maps** — custom PNG tiles are saved alongside `.csm` files and reloaded correctly in both solo test-play and multiplayer
- **Editor direct re-save** — Ctrl+S on an already-saved map skips the mod-save dialog and writes directly to the existing path
- **Wayland-first display driver** — game prefers Wayland over X11 on Linux (`SDL_VIDEODRIVER=wayland,x11`), overridable by environment
- **Bomb explosions leave scorch marks** — explosion decals use `DecalType::Scorch` (dark, near-black) instead of blood-coloured particles; enemy-death blood remains red

### v0.5.9
- **Fix: enemies not spawning in solo play** — PvP flag from a previous multiplayer session was not reset when starting a new solo game; `startGame()` now explicitly resets `lobbySettings_.isPvp` so wave spawning always works in solo mode
- **Version displayed in main menu** — version number now shown under the "COLD START" title on the main menu
- **UPnP port forwarding** — when hosting a multiplayer game the game now sends a UPnP port-mapping request to the router automatically (async background thread, requires miniupnpc; gracefully no-ops if not available)

### v0.5.8
- **No damage invulnerability in PvP** — `invulnDuration` set to `0` in PvP modes (was 10 ms); rapid hits always register with no grace window
- **Bomb homing targets enemy players** — dashed bombs now lock onto the nearest enemy player (remote players on opposing teams) in PvP within a 45° cone, in addition to AI enemies
- **Bomb–player collision** — dashed bombs now explode on contact with and proximity to remote enemy players; host-authoritative explosion damage applies to all players hit

### v0.5.7 (2026-03-05)
- **Team spawn triggers** — editor now supports `TeamSpawnRed/Blue/Green/Yellow` trigger types; multiplayer spawns and respawns use the matching trigger when team mode is active, falling back to corner logic for generated maps
- **Host-authoritative PvP damage** — bullet and explosion damage in PvP/team modes is now validated and applied by the host; clients report hits via `HitRequest` and receive authoritative `PlayerHpSync` packets, eliminating client-side god-mode cheating
- **Higher network update rates** — enemy state packets 10 Hz → 20 Hz; player state packets 20 Hz → 30 Hz
- **Version tag** — `v0.5.7` shown in main menu corner

### v0.5.6
- PvP gamemode rules: wave spawning disabled, bombs deal 3 HP area splash, 10 ms damage invulnerability in PvP (vs 700 ms in PvE)

### v0.5.5
- Enemy AI overhaul: 360° vision, multi-player targeting, 30 s idle retarget, client-side interpolation, dash trail/flash visual, 26-byte enemy state packet
- Max players raised from 16 to 128
