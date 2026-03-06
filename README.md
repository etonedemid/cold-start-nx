# COLD START

COLD START is a top-down action shooter built in C++ with SDL2 for PC and Nintendo Switch homebrew. It combines fast combat, local content editing, multiplayer support, and a lightweight modding pipeline aimed at rapid iteration.

## Highlights

- PC and Nintendo Switch builds from the same codebase
- Arena, co-op, deathmatch, team deathmatch, playlist, and custom-map play modes
- Host-authoritative online multiplayer with ENet-based state sync
- Procedural and custom maps, map packs, character content, and upgrade crates
- In-game map editor and built-in pixel-art texture editor
- Mod loading with support for maps, packs, characters, items, gamemodes, sprite overrides, sound overrides, and synced multiplayer mod payloads

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
