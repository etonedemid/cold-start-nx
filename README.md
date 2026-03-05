
<img width="1286" height="753" alt="image" src="https://github.com/user-attachments/assets/4c0f3ae1-f8fa-4d35-b2d5-9100a5d36809" />

<img width="1286" height="753" alt="image" src="https://github.com/user-attachments/assets/3378a858-5b7a-4f63-ad82-46a45cb115fe" />

<img width="1286" height="753" alt="image" src="https://github.com/user-attachments/assets/60443747-97fa-4008-9706-8a48550caa94" />

# COLD START (NX + PC)

Top-down 2D shooter built with C++ and SDL2.

- Nintendo Switch homebrew target (devkitPro/libnx)
- PC target (Linux/macOS/Windows via CMake + SDL2)
- Custom map/pack support
- Character creator and in-game editor

## Features

- Fast arena-style combat with melee + shooter enemies
- Bomb system (orbit, launch, homing, explosions)
- Blood/scorch decals and particle effects
- Configurable gameplay and audio settings
- Custom maps (`.csm`) and map packs

## Requirements

### Switch build

- devkitPro toolchain
- libnx
- switch SDL2 libraries (image/ttf/mixer)

### PC build

- CMake (>= 3.14)
- C++17 compiler
- SDL2, SDL2_image, SDL2_ttf, SDL2_mixer
- pkg-config

## Build

### Nintendo Switch

```bash
cd cold_start
make -j4
```

Output:

- `cold_start.nro`

### PC

```bash
cd cold_start
mkdir -p build-pc
cd build-pc
cmake ..
make -j4
```

Output:

- `build-pc/cold_start`

## Run

### PC

```bash
cd cold_start/build-pc
./cold_start
```

### Switch (nxlink example)

```bash
cd cold_start
/opt/devkitpro/tools/bin/nxlink -a <SWITCH_IP> -s cold_start.nro
```

## Controls

### Gamepad

- Move: left stick
- Aim: right stick
- Shoot: RT
- Parry: LB
- Bomb: LT
- Pause: Start
- Menu confirm/back: B / A

### Keyboard (PC)

- Move: WASD
- Aim: mouse
- Shoot: left mouse
- Parry: Space
- Bomb: Q
- Pause: Esc
- Menu: arrows / Enter / Backspace

## Project Structure

- `source/` — game code
- `romfs/` — runtime assets (sprites/sounds/fonts)
- `build-pc/` — PC build directory
- `maps/` — custom maps
- `mappacks/` — map pack campaigns
- `characters/` — custom characters

## Notes

- On first run, some content directories may be created automatically.
- Config is saved to `config.txt`.
