
<img width="848" height="204" alt="banner" src="https://github.com/user-attachments/assets/2a0af91e-a15e-462f-aa99-d2869a311675" />

# COLD START  `v0.5.9`

COLD START is a top-down action shooter built in C++ with SDL2 for PC and Nintendo Switch homebrew. It combines fast combat, local content editing, multiplayer support, and a lightweight modding pipeline aimed at rapid iteration.

## Highlights

- PC and Nintendo Switch builds from the same codebase
- Arena, co-op, deathmatch, team deathmatch, playlist, and custom-map play modes
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

### Switch via nxlink

```bash
cd cold_start
nxlink -a <SWITCH_IP> -s cold_start.nro
```

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
