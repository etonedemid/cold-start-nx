
<img width="848" height="204" alt="banner" src="https://github.com/user-attachments/assets/2a0af91e-a15e-462f-aa99-d2869a311675" />

# COLD START  `v1.7.0`

COLD START is a top-down action shooter built in C++ with SDL2 for PC and Nintendo Switch homebrew. It combines fast combat, local content editing, multiplayer support, and a lightweight modding pipeline aimed at rapid iteration.

# Welcome, operator.

## Screenshots

![screenshot 1](https://img.itch.zone/aW1hZ2UvMzczNzA2OS8yNzQ4MDEwOC5wbmc=/original/Fq6lHp.png)
![screenshot 2](https://img.itch.zone/aW1hZ2UvMzczNzA2OS8yNzQ4MDEzMy5qcGc=/original/lUvkVG.jpg)
![screenshot 3](https://img.itch.zone/aW1hZ2UvMzczNzA2OS8yNzQ4MDEzNi5qcGc=/original/ZFEjh9.jpg)
![screenshot 4](https://img.itch.zone/aW1hZ2UvMzczNzA2OS8yNzQ4MDEzOS5qcGc=/original/r7fCy%2F.jpg)
![screenshot 5](https://img.itch.zone/aW1hZ2UvMzczNzA2OS8yNzQ4MDE0MS5qcGc=/original/fvrs9n.jpg)



## Feature Set

### Gameplay

- Melee enemies, projectile enemies, bombs, parry, pickups, and upgrade crates
- PvE and PvP rule sets with configurable lives, friendly fire, teams, wave count, spawn scaling, and crate intervals
- Spectating, respawning, score tracking, and lobby flow for multiplayer sessions


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

server quick setup:

```bash
cd cold_start
chmod +x deploy/digitalocean/install_server.sh
./deploy/digitalocean/install_server.sh
sudo cp deploy/digitalocean/cold_start.service /etc/systemd/system/cold_start.service
sudo systemctl daemon-reload
sudo systemctl enable --now cold_start
sudo ufw allow 7777/udp
```

## Release artifacts

Current release artifacts for `v1.7.0`:

- `cold_start-linux-v1.7.0.zip` — Linux x86_64 (self-contained, SDL2 libs bundled)
- `cold_start-linux-server-v1.7.0.zip` — Linux dedicated server (headless)
- `cold_start-windows-v1.7.0.zip` — Windows x86_64 (MinGW, all DLLs bundled)
- `cold-start-nx.nro` — Nintendo Switch homebrew

## Project Layout

- `source/` — gameplay, networking, editors, modding, and core systems
- `romfs/` — bundled runtime assets
- `maps/` — standalone maps
- `characters/` — character definitions
- `tiles/` — tile content
- `build-pc/` — CMake build output for desktop
- `build-win/` — CMake build output for Windows cross-builds

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

### Multiplayer mod sync

When the host starts a multiplayer match, enabled mods are serialized and sent to clients before gameplay begins.

Synced content now includes:

- custom maps and packs
- character folders and configs
- sprite and tile overrides
- sound and music overrides
- item and gamemode definitions
- `mod.cfg` metadata and gameplay overrides

Security:

- custom character sync is capped at 10 MB per player
- multiplayer mod sync is capped at 64 MB total per lobby start
- synced mod IDs and file paths are validated before any files are written
- oversized, malformed, or traversal-style payloads are rejected
- synced mods install into `mods/_mp_sync/` and that directory is rebuilt on each fresh sync

trust model:

- clients should only join hosts they trust
- synced mods are treated as content, not native code, but they can still replace gameplay assets and override balance values
- the host decides which enabled mods are active for that session

notes:

- large synced media can make match start take longer on slower devices
- if a file would push the sync over the 64 MB cap, it is skipped from the transfer
- local non-network mods remain separate from the temporary `mods/_mp_sync/` install area
