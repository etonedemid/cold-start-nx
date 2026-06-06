# Cold Start - Project Memory
USE ONLY ASCII

## Projects (all on Z: drive)
- `Z:\cold-start-nx` - the game (C++/SDL2, targets Switch NRO, PC ELF, Android APK)
- `Z:\coldstart-workshop-site` - static HTML/JS frontend (Win98 aesthetic), GitHub: `etonedemid/coldstart-workshop`
- `Z:\workshop-server` - Python FastAPI backend, GitHub: `etonedemid/cs-workshop-backend`

## Network
- Dev machine: Windows PC (this machine), Z: drive is the project root
- Workshop laptop: IP/credentials stored in `Z:\cold-start-nx\.env.local` (gitignored)
- Workshop runs on laptop at port 8080, Caddy proxies it at `https://coldstartworkshop.duckdns.org`
- Caddy: `sudo systemctl restart caddy` on the laptop
- Server: `cd ~/workshop-server && nohup python3 main.py > server.log 2>&1 &`

## Deploy workflow
Run `Z:\cold-start-nx\deploy_all.ps1` - commits+pushes both frontend and backend to GitHub, SCPs changed server files to laptop, restarts server and Caddy.

## Mod system
- Game mods live in `mods/<folder_id>/mod.cfg` (INI format, `[mod]` section, `id=` field = folder name)
- Workshop backend stores mods by UUID in `data/mods/<uuid>/`, mod zip may contain `mod.cfg`
- `mod_cfg_id` DB field = the `id` from inside the mod.cfg (= the install folder name in-game)
- API returns `folder_id` which the game uses as the install directory name
- `modconfig.cfg` in game root tracks which mods are enabled

## Backend (workshop-server)
- FastAPI, SQLite (`data/workshop.db`), uvicorn port 8080
- Startup: `db.init_db()` + `db.scan_and_recover_approved_mods()` (recovers disk-only mods)
- Vote endpoint: `POST /api/v1/mods/{id}/vote?upvote=true|false`
- Sort: `GET /api/v1/mods?sort=newest|name|score|downloads`

## Game states relevant to workshop
- `GameState::OnlineWorkshop` - browse/download mods from the internet (`renderOnlineWorkshop`)
- `GameState::Workshop` - in-game pause workshop browser of installed mods (`renderWorkshopMenu`)

## Key files changed in this session
- `source/game.h` - `OnlineModInfo` struct (added folder_id, votes, type, downloads)
- `source/netmenus.cpp` - `parseModListJSON`, `downloadAndInstallMod`, `renderOnlineWorkshop`
- `source/render.cpp` - `renderWorkshopMenu` (remade as full browser mirroring web layout)
- `workshop-server/db.py` - schema, votes, mod_cfg_id, disk scan, sort
- `workshop-server/server.py` - lifespan startup scan, vote endpoint, folder_id in API
- `coldstart-workshop-site/index.html` - Fetching state, sort dropdown, vote buttons
- `coldstart-workshop-site/js/api.js` - fetchMods sort param, voteOnMod()
