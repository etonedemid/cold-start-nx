# Cold Start NX - Modding Reference

Version: 3.4.0 (engine v2.5.1)

---

## Overview

Cold Start NX supports user-created content via four file formats:

| Format | Extension | What it is |
|---|---|---|
| Map | `.csm` | A playable level (tiles, triggers, enemy spawns) |
| Map pack | `.cspack` | An ordered playlist of maps with story config |
| Cutscene library | `.csc` | Text script that lives alongside a `.csm` |
| Character | `character.cfg` + PNGs | Player skin with custom stats |

All content lives inside **romfs/** at runtime (or the folder next to the binary on PC).
Maps and packs you create should go inside a subfolder of `romfs/maps/`.

---

## romfs Layout

```
romfs/
  characters/         -- player skins
    Jacket/
      character.cfg
      body-0001.png .. body-0011.png
      legs-0001.png .. legs-0012.png
      death-1.png .. death-12.png
  fonts/              -- game fonts (do not modify)
  maps/               -- all maps and packs
    mymission/
      level1.csm
      level1.csc      -- same stem as the .csm
      level2.csm
      level2.csc
      pack.cspack
      icon.png        -- optional pack icon
  sounds/             -- audio files
    action/           -- in-game music
  sprites/            -- UI / HUD sprites
  tiles/
    ground/           -- floor tile textures (64x64 px)
    walls/            -- wall / solid tile textures (64x64 px)
    ceiling/          -- ceiling / overlay tile textures (64x64 px)
    props/            -- static prop textures
```

---

## Map Pack (.cspack)

A map pack defines a sequence of levels and optional story configuration.
It is a plain INI text file.

### Minimal example

```ini
[pack]
name=My Campaign
creator=YourName
description=Three levels in an abandoned zone.
version=1

[character]
path=characters/Jacket/character.cfg

[maps]
count=3
map1=maps/mymission/level1.csm
map2=maps/mymission/level2.csm
map3=maps/mymission/level3.csm
```

### Full example with all optional fields

```ini
[pack]
name=Sector 9 Extended
creator=Etone
description=Fan expansion set in the industrial outer zones.
version=2
icon=maps/mymission/icon.png
tags=action,story,hard

[character]
path=characters/Jacket/character.cfg
path2=characters/AltChar/character.cfg

[maps]
count=2
map1=maps/mymission/level1.csm
name1=Cold Dawn
desc1=Escape the storage district.
music1=maps/mymission/level1_theme.mp3
map2=maps/mymission/level2.csm
name2=Zone 10
desc2=Infiltrate the evacuated civic zone.
```

### [pack] keys

| Key | Type | Description |
|---|---|---|
| `name` | string | Pack display name |
| `creator` | string | Author name |
| `description` | string | Short description (shown in pack select) |
| `version` | int | Pack version number |
| `icon` | path | Path to icon image (relative to romfs/, or absolute) |
| `tags` | string | Comma-separated tags for search/filter |

### [character] keys

| Key | Description |
|---|---|
| `path` | Path to first character's `character.cfg` |
| `path2`, `path3`, ... | Additional character paths |

### [maps] keys

| Key | Description |
|---|---|
| `count` | Number of maps |
| `mapN` | Path to the Nth `.csm` file (N starts at 1) |
| `nameN` | Display name override for map N |
| `descN` | Short description for map N |
| `musicN` | Music override for map N (path relative to romfs/) |

---

## Map Format (.csm)

Maps are created and edited with the in-game map editor (press F1 from the
main menu, or use the editor from the pack browser). The `.csm` binary format
is not hand-editable; use the editor.

### Game modes

| Value | Mode |
|---|---|
| 0 | Arena (wave survival - enemies spawn in waves) |
| 1 | Sandbox (free roam, gameplay fully controlled by cutscenes/triggers) |

### Trigger types

Triggers are rectangular zones placed in the editor.

| ID | Name | Fires when... | param |
|---|---|---|---|
| 0 | LevelStart | player spawn point | - |
| 1 | LevelEnd | player enters (must satisfy condition) | GoalCondition |
| 2 | Crate | places a breakable crate | - |
| 3 | Effect | visual/audio zone | effect ID |
| 10 | TeamSpawnRed | team 0 spawn (PvP) | - |
| 11 | TeamSpawnBlue | team 1 spawn | - |
| 12 | TeamSpawnGreen | team 2 spawn | - |
| 13 | TeamSpawnYellow | team 3 spawn | - |
| 14 | LayerFade | top image layer fades when player inside | - |
| 15 | CollisionZone | invisible solid rectangle (can be rotated) | - |
| 16 | Cutscene | plays a cutscene | index into .csc library |
| 17 | Waypoint | commits route A or B | 1=Spearhead, 2=Signal |
| 18 | SignalZone | one-time SIGNAL delta | signed delta (int8) |
| 19 | Objective | side-request marker | ObjectiveKind |
| 20 | SetVariable | applies a variable action from the .csc config | action index |
| 21 | LoadMap | transitions to another map | path in .csc config |

### LevelEnd goal conditions

| Value | Meaning |
|---|---|
| 0 | DefeatAll - kill all enemies |
| 1 | OnTrigger - activated by stepping on a specific trigger |
| 2 | Immediate - always open |
| 3 | OnFlag - open when a story flag is set |

---

## Cutscene Library (.csc)

Every `.csm` can have a `.csc` sidecar with the same filename stem.
The `.csc` is a plain-text script loaded automatically with the map.
Create and edit it with the in-game cutscene editor (F4 from map editor).

A `.csc` file contains:
- One or more `[cutscene]` blocks
- `[dialog]` blocks for dialog sequences
- A `[config]` block for library-wide settings
- `[trigger_var]` blocks for variable actions attached to SetVariable triggers
- `[trigger_cond]` blocks for conditions that gate any trigger
- `[trigger_map]` blocks for map paths used by LoadMap triggers

### File header

```
# Cold Start Cutscene Library v2
[library]
version=2
```

### [config]

```ini
[config]
ondeath=death_screen_cs
```

`ondeath` is the cutscene ID to play when the player dies (story/pack modes only).
Inside this cutscene a `DeathScreen` event will show the actual death screen UI.

---

## Cutscenes

### [cutscene]

```ini
[cutscene]
id=intro
block_input=1
chain_on_end=next_cutscene_id
```

| Field | Type | Description |
|---|---|---|
| `id` | string | Unique ID referenced by triggers and other cutscenes |
| `block_input` | 0/1 | Whether player input is locked during playback |
| `chain_on_end` | string | Cutscene ID to auto-start when this one finishes |

---

## Actors

Each cutscene can define actors. The player and enemies are special built-in
types; free sprites load any PNG.

```ini
[actor]
id=1001
name=Avi
type=1
enemy_type=5
start_x=640.0
start_y=360.0
start_rot=0.0
start_scale_x=1.0
start_scale_y=1.0
start_alpha=1.0
start_visible=1
flip_h=0
layer=2
```

| Field | Type | Description |
|---|---|---|
| `id` | uint | Unique actor ID within this cutscene |
| `name` | string | Label shown in editor |
| `type` | int | 0=Player, 1=Enemy, 2=FreeSprite |
| `enemy_type` | int | 0=Melee 1=Shooter 2=Brute 3=Scout 4=Sniper 5=Gunner |
| `sprite_path` | path | PNG path (FreeSprite only, relative to romfs/) |
| `start_x/y` | float | World position at cutscene start |
| `start_rot` | float | Starting rotation in degrees (0=up) |
| `start_scale_x/y` | float | Starting scale (1.0 = normal) |
| `start_alpha` | float | Starting opacity 0.0-1.0 |
| `start_visible` | 0/1 | Whether actor is visible at start |
| `flip_h` | 0/1 | Horizontal flip |
| `layer` | int | Draw order. Higher value = drawn on top. Default 0. |

---

## Events

Events drive everything that happens during a cutscene.
All events share a set of base fields:

```ini
[event]
actor_id=1001
type=0
t0=0.0
dur=1.0
ease=0
```

| Field | Type | Description |
|---|---|---|
| `actor_id` | uint | Which actor this event targets. 0 = camera/global |
| `type` | int | Event type (see table below) |
| `t0` | float | Start time in seconds |
| `dur` | float | Duration in seconds (0 for instant events) |
| `ease` | int | 0=Linear 1=EaseIn 2=EaseOut 3=EaseInOut 4=Instant |

### Event types

| ID | Name | Purpose |
|---|---|---|
| 0 | Move | Moves actor from one position to another |
| 1 | Rotate | Rotates actor |
| 2 | Scale | Scales actor |
| 3 | Alpha | Changes opacity |
| 4 | Flash | Overlays a color flash |
| 5 | Wait | No-op delay |
| 6 | Dialog | Shows a dialog sequence |
| 7 | PlaySFX | Plays a sound file |
| 8 | SpawnExplosion | Visual explosion at a world position |
| 9 | CameraMove | Moves camera to a position |
| 10 | CameraZoom | Zooms camera |
| 11 | CameraShake | Shakes the camera |
| 12 | ScreenFade | Fades screen to/from black |
| 13 | CinematicBars | Shows/hides black bars at top and bottom |
| 14 | SetVisible | Shows or hides an actor |
| 15 | SetFrame | Sets a specific sprite animation frame |
| 16 | SpawnActor | Makes an actor appear (optionally at a position) |
| 17 | DespawnActor | Hides/removes an actor |
| 18 | SetFlag | Sets a named script flag |
| 19 | ChainCutscene | Starts another cutscene by ID |
| 20 | EndCutscene | Immediately ends this cutscene |
| 21 | AdjustSignal | Adds to the SIGNAL meter |
| 22 | BranchCutscene | Conditionally chains one of two cutscenes |
| 23 | SpawnEnemy | Spawns a live enemy at a world position |
| 24 | SpawnPickup | Spawns an upgrade pickup at a world position |
| 25 | CameraRotate | Rotates the camera viewport |
| 26 | SetVariable | Modifies a named integer variable |
| 27 | DeathScreen | Triggers death screen (ondeath cutscene only) |
| 28 | LoadMap | Transitions to another map |

---

### Event field reference

Fields not listed for a given type are ignored.

#### Move (type=0)
```ini
from_x=100.0  from_y=200.0
to_x=500.0    to_y=200.0
```

#### Rotate (type=1)
```ini
from_rot=0.0
to_rot=90.0
```
Degrees clockwise. 0 = facing up.

#### Scale (type=2)
```ini
from_scale_x=1.0  from_scale_y=1.0
to_scale_x=2.0    to_scale_y=2.0
```

#### Alpha (type=3)
```ini
from_alpha=1.0
to_alpha=0.0
```

#### Flash (type=4)
```ini
from_alpha=0.0
to_alpha=1.0
flash_r=255  flash_g=80  flash_b=80
```

#### Dialog (type=6)
```ini
dialog_id=my_sequence_id
```
Points to a `[dialog]` block defined elsewhere in this file.

#### PlaySFX (type=7)
```ini
sfx_path=sounds/beep.mp3
```
Path relative to romfs/.

#### SpawnExplosion (type=8)
```ini
expl_x=640.0  expl_y=360.0
```

#### CameraMove (type=9)
```ini
from_x=320.0  from_y=180.0
to_x=960.0    to_y=540.0
```
World coordinates of the camera center.

#### CameraZoom (type=10)
```ini
from_zoom=1.0
to_zoom=2.0
```

#### CameraShake (type=11)
```ini
shake_strength=8.0
```

#### ScreenFade (type=12)
```ini
fade_to_black=1
```
1 = fade to black, 0 = fade from black.

#### CinematicBars (type=13)
```ini
show_bars=1
```
1 = show bars, 0 = hide bars.

#### SetVisible (type=14)
```ini
visible=0
```

#### SetFrame (type=15)
```ini
frame=3
```

#### SpawnActor (type=16)
```ini
spawn_override_pos=1
spawn_x=400.0  spawn_y=300.0
```
`spawn_override_pos=0` places actor at its `start_x/y`.

#### SpawnEnemy (type=23)
```ini
expl_x=400.0  expl_y=300.0
spawn_enemy_type=0
```
Enemy type IDs: 0=Melee 1=Shooter 2=Brute 3=Scout 4=Sniper 5=Gunner

#### SpawnPickup (type=24)
```ini
expl_x=400.0  expl_y=300.0
spawn_pickup_type=0
```
Uses the UpgradeType enum (see upgrade list below).

#### CameraRotate (type=25)
```ini
from_rot=0.0
to_rot=45.0
```
Degrees clockwise. Rotates the entire viewport.

#### SetVariable (type=26)
```ini
var_name=kills
var_value=1
var_op=1
var_scope=0
```
| Field | Values |
|---|---|
| `var_op` | 0=Set, 1=Add, 2=Subtract |
| `var_scope` | 0=Local (resets on level reload), 1=Pack (persists across maps) |

#### LoadMap (type=28)
```ini
map_path=maps/mymission/level2.csm
```
Path relative to romfs/.

#### BranchCutscene (type=22)
```ini
branch_var=2
branch_cmp=2
branch_threshold=3
chain_cs_id=good_end
chain_false_id=bad_end
flag_name=kills
```
| `branch_var` | What is compared |
|---|---|
| 0 | SIGNAL meter (0-100) |
| 1 | Route (0=undecided, 1=Spearhead, 2=Signal) |
| 2 | Named variable (use `flag_name` as variable name) |

| `branch_cmp` | Operator |
|---|---|
| 0 | > |
| 1 | != |
| 2 | == |
| 3 | < |
| 4 | >= |
| 5 | <= |

If the condition is true the cutscene chains to `chain_cs_id`.
If false it chains to `chain_false_id`. Either can be empty (ends cutscene).

#### SetFlag (type=18)
```ini
flag_name=rescued_tally
flag_value=1
```
Script flags are boolean. They persist for the life of a cutscene and are
accessible to `BranchCutscene` (branchVar mode is separate from variables).
Use variables for integer state.

#### ChainCutscene (type=19)
```ini
chain_cs_id=act2_intro
```

#### AdjustSignal (type=21)
```ini
signal_delta=10
```
Positive = raise SIGNAL. Negative = lower SIGNAL.

---

## Dialog Sequences

```ini
[dialog]
id=intro_seq
[line]
char=MARROW
portrait=sprites/marrow_portrait.png
text=Handshake confirmed at the relay. So you actually did it.
pleft=1
sfx=sounds/beep.mp3
[line]
char=MARROW
text=Okay, operator. We see what it sees.
pleft=1
[line]
char=AVA
text=We are no longer addressing the machine.
pleft=0
[choice]
text=Keep going
next=keep_seq
set_flag=chose_keep
set_flag_value=1
[choice]
text=Turn back
next=
```

### [dialog] fields

| Field | Description |
|---|---|
| `id` | Unique ID referenced by Dialog events |

### [line] fields

| Field | Description |
|---|---|
| `char` | Speaker name shown above the text |
| `portrait` | Path to portrait PNG (relative to romfs/), empty = no portrait |
| `text` | Dialog text |
| `pleft` | 1 = portrait on left, 0 = portrait on right |
| `sfx` | Sound to play when this line starts |

### [choice] fields (optional, attached to the preceding [line])

| Field | Description |
|---|---|
| `text` | Choice label shown to player |
| `next` | Dialog sequence ID to jump to, empty = end dialog |
| `set_flag` | Flag name to set when this choice is selected |
| `set_flag_value` | Boolean value to set the flag to (1/0) |

If a `[line]` has one or more `[choice]` entries, a choice menu appears after
the line finishes typing.

---

## Variables

Variables are named integers accessible from triggers and cutscene events.

### Scopes

- **Local** (`var_scope=0`): Reset to zero when the level is reloaded or
  when a new level starts within a pack. Use for per-level state.
- **Pack** (`var_scope=1`): Persist across all maps in a pack run.
  Cleared only when a new pack run starts. Use for campaign-wide flags.

### Using variables in triggers

Define a `[trigger_var]` block to attach a variable action to a `SetVariable`
trigger zone. The trigger's `param` byte is the 0-based index into the list
of `[trigger_var]` blocks.

```ini
[trigger_var]
trigger_index=0
key=lights_out
value=1
op=0
scope=1
```

| Field | Description |
|---|---|
| `trigger_index` | Which trigger zone in the map (0-based) |
| `key` | Variable name |
| `value` | Integer value |
| `op` | 0=Set, 1=Add, 2=Subtract |
| `scope` | 0=Local, 1=Pack |

### Gating any trigger with a condition

```ini
[trigger_cond]
trigger_index=2
var_name=lights_out
value=1
cmp=2
```

| `cmp` | Operator |
|---|---|
| 0 | == (equal) |
| 1 | != (not equal) |
| 2 | > (greater than) |
| 3 | < (less than) |
| 4 | >= (greater or equal) |
| 5 | <= (less or equal) |

Multiple `[trigger_cond]` blocks with the same `trigger_index` are ANDed:
all conditions must pass for the trigger to fire.

### Loading a map from a trigger

```ini
[trigger_map]
trigger_index=5
map_path=maps/mymission/level2.csm
```

Pair this with a `LoadMap` trigger zone (type 21) in the map.

---

## Complete .csc Example

```
# Cold Start Cutscene Library v2
[library]
version=2

[config]
ondeath=player_died

[cutscene]
id=player_died
block_input=1
chain_on_end=
[event]
actor_id=0
type=13
t0=0.0
dur=0.3
show_bars=1
[event]
actor_id=0
type=12
t0=0.0
dur=0.5
fade_to_black=1
[event]
actor_id=0
type=27
t0=0.6
dur=0.0

[cutscene]
id=level_start
block_input=1
chain_on_end=
[actor]
id=1001
name=Guard
type=1
enemy_type=0
start_x=640.0
start_y=360.0
start_visible=0
layer=1
[event]
actor_id=0
type=13
t0=0.0
dur=0.4
show_bars=1
[event]
actor_id=0
type=12
t0=0.0
dur=0.5
fade_to_black=0
[event]
actor_id=0
type=6
t0=0.6
dur=3.0
dialog_id=opening_lines
[event]
actor_id=1001
type=16
t0=0.5
dur=0.0
spawn_override_pos=0
[event]
actor_id=0
type=13
t0=3.5
dur=0.4
show_bars=0

[dialog]
id=opening_lines
[line]
char=MARROW
text=Zone 10. Evacuated. Watch for the perimeter drones.
pleft=1

[trigger_var]
trigger_index=3
key=door_open
value=1
op=0
scope=0

[trigger_cond]
trigger_index=4
var_name=door_open
value=1
cmp=0

[trigger_map]
trigger_index=7
map_path=maps/mymission/level2.csm
```

---

## Characters (.cfg + PNGs)

Create a folder inside `romfs/characters/YourCharacter/`.

### character.cfg

```ini
name=Your Character

speed=520
hp=100
ammo=10
fire_rate=10.0
reload_time=1.0
shoot_x=12
shoot_y=-32
```

| Field | Default | Description |
|---|---|---|
| `name` | (folder name) | Display name |
| `speed` | 520 | Movement speed (pixels/sec) |
| `hp` | 100 | Max HP |
| `ammo` | 10 | Magazine size |
| `fire_rate` | 10.0 | Shots per second |
| `reload_time` | 1.0 | Reload duration in seconds |
| `shoot_x` | 12 | Bullet spawn X offset from player center |
| `shoot_y` | -32 | Bullet spawn Y offset from player center |

### Required sprite files

All PNGs must be square and consistently sized (recommend 64x64 or 128x128).

| Files | Count | Notes |
|---|---|---|
| `body-0001.png` ... `body-0011.png` | 11 | Upper body frames. Frame 1 is idle. Frames 4-10 are melee swing. |
| `legs-0001.png` ... `legs-0012.png` | 12 | Walking animation frames |
| `death-1.png` ... `death-12.png` | 12 | Death animation frames |

The game auto-detects all PNG files in the folder by the naming pattern above.
Missing frames will fall back to the previous frame where possible.

---

## Upgrade Types

Used by `SpawnPickup` events (`spawn_pickup_type` field) and the
`give` console command.

| ID | Name |
|---|---|
| 0 | SpeedUp |
| 1 | DamageUp |
| 2 | FireRateUp |
| 3 | AmmoUp |
| 4 | HealthUp |
| 5 | ReloadUp |
| 6 | Blindness |
| 7 | BombPickup |
| 8 | Magnet |
| 9 | Ricochet |
| 10 | TripleShot |
| 11 | Overclock |
| 12 | HeavyRounds |
| 13 | BombCore |
| 14 | Juggernaut |
| 15 | StunRounds |
| 16 | Scavenger |
| 17 | ExplosiveTips |
| 18 | ChainLightning |
| 19 | RailSlugs |
| 20 | SharpenedEdge |
| 21 | Bloodlust |
| 22 | ShockEdge |
| 23 | AutoReloader |
| 24 | Vampire |
| 25 | LastStand |
| 26 | QuickParry |
| 27 | ParrySurge |
| 28 | ReactiveParry |
| 29 | SlowDown (cursed) |
| 30 | GlassCannon (cursed) |

---

## Console Commands

Open the console with `~` during gameplay (story/custom map modes).
All spawn commands place the entity at the current mouse cursor position.

| Command | Description |
|---|---|
| `wave N` | Force the current wave to N |
| `hp N` | Set player HP |
| `god` | Toggle god mode |
| `clear` | Kill all active enemies |
| `bombs N` | Set bomb count |
| `signal [N]` | Set SIGNAL to N, or print current value |
| `spawn <type> [N]` | Spawn N enemies of type at mouse position |
| `spawn_box` | Spawn a destructible crate at mouse position (no contents) |
| `vehicle car` | Spawn a car at mouse position |
| `give <upgrade>` | Give player an upgrade by name (partial match ok) |
| `help` | Print command list |

Spawn enemy types: `melee`, `shooter`, `brute`, `scout`, `sniper`, `gunner`,
`boss_brute`, `boss_sniper`, `boss_gunner`.

---

## The SIGNAL System

SIGNAL is a 0-100 integer representing the Relay's trust in the player.
It is shared across the whole pack run (pack scope).

Story trigger zones can adjust SIGNAL via `SignalZone` triggers (param = signed
delta, stored as int8: range -128 to +127 in editor, practical range -50 to +50).

Cutscene events use `AdjustSignal` (type 21, `signal_delta` field).

SIGNAL can be read in `BranchCutscene` events (`branch_var=0`) to drive
branching narrative.

---

## Multiplayer Notes

Custom maps work in online co-op and PvP modes. The host's file system is
authoritative for `.csm` files; clients do not need to have the map installed.
Cutscene `.csc` files are not currently synced to clients - run cutscenes only
in solo or local-coop contexts unless you know what you are doing.

---

## Workshop

Maps and packs can be uploaded and downloaded from the workshop at:

    https://coldstartworkshop.duckdns.org

The workshop browser is accessible from the main menu. Pack folders are
uploaded as a zip containing the `.cspack` file and all referenced `.csm`,
`.csc`, and asset files.
