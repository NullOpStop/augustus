# New Frontier mode

A procedural game mode for this Augustus fork, on the long-lived `frontier-mode` branch.
Start here; details live in the linked docs.

- [WORLD_GENERATION.md](WORLD_GENERATION.md): how worlds are generated, engine constraints, design decisions.

## Vision

Empire Earth-style start: the player arrives at a single spot on a generated world hidden by fog of
war, then explores outward to find land, water and resources. A pre-game screen sets the world and the
economy. Later: real resource deposits, a generated empire map with trade, generated events.

The world is generated whole before play rather than "endless": the engine has a fixed grid, and a
pre-generated world can be designed to be coherent and playable. Target size 512x512 (1024 as an option
after profiling) once the grid is widened; until then worlds are at most 160x160.

## Decisions (from the project owner)

| Topic | Decision |
|---|---|
| Repository | Branch `frontier-mode` in this fork, not a separate repo. Upstream base is tag `augustus-snapshot-2026-10-10` (Keriew/augustus master `ae15d133d`). Not pushed yet. |
| World size | A setting on the pre-game screen. Default 512 once possible; 1024 only after profiling. |
| World shape | Shaped worlds: templates (river valley, coastal, lakes, highlands, island) plus noise for detail. |
| Elevation | Landmarks, not the ground: most land on level 0, a few smooth plateaus with access ramps. Noise terraces everywhere were rejected after the first play as "messy, rough, ugly, no soul". |
| Arrival | A long road in from the world edge is fine. |
| Start | No start building. The road simply ends at the start site; the fog is lifted around it. |
| Resources | Real deposits (a mode-only mechanic) where mines and quarries need the matching deposit. Not built yet. |
| Way of working | Grow the in-game preview with every generator feature ("like Empire Earth's map setup"). Favour progress; polish later. |

## Status (2026-10-10)

Commits on `frontier-mode`, oldest first:

| Commit | What |
|---|---|
| `47f300c95` | World model, seeded RNG, simplex noise, PNG preview CLI |
| `ab8c27420` | "New Frontier" main-menu entry and setup window with a live isometric preview |
| `23e3edaab` | Rivers and lakes (drainage simulation, later replaced) |
| `f84fc1152` | Start site, access ramps between levels, the road in and out |
| `08638c921` | Fog of war |
| `f99e943d9` | Generator redesign: worlds composed from features, benchmarked against the original maps |

Playable now: main menu → New Frontier → choose world, climate, size, water, relief, forest, seed →
Start. The world is generated, saved as `frontier_world.mapx` in the scenario folder and started with
fog of war around the start site.

Waiting on: the owner's verdict on the redesigned terrain. It now measures like hand-made maps but
lacks memorable features ("soul"). Proposed: signature landmarks such as a great lake with an island,
a river delta, a lone stepped mountain, a gorge, an ancient forest, native villages or ruins.

## Code map

All mode code lives in its own files; existing modules only get small hooks.

| Path | What |
|---|---|
| `src/frontier/worldgen/` | The generator. Independent of the engine grid; see WORLD_GENERATION.md. |
| `src/frontier/world_export.c` | Stamps a generated world into the engine grids (terrain, elevation, points, images, ramps). |
| `src/frontier/frontier_game.c` | Generate → export → save `frontier_world.mapx` → start it, enable fog, reveal the start. |
| `src/frontier/dev_render.c` | `--worldgen-render` developer option (see below). |
| `src/frontier/map_stats.c` | Logs `MAPSTATS` lines describing a map, for comparing generated with hand-made maps. |
| `src/window/frontier_setup.c` | Setup window. Strings are hard-coded English for now. |
| `src/map/fog.c` | Fog of war: explored grid, reveal, per-tick update, save/load. |
| `tools/worldgen_preview/` | PNG preview CLI and `render_in_game.sh`. |

Fog of war hooks into existing code: `city/view.c` (skip hidden tiles), `widget/minimap.c`,
`map/tiles.c` and `map/building_tiles.c` (no building on hidden tiles), `widget/city/building_ghost.c`,
`map/routing.c` (road/wall/aqueduct placement), `widget/city/city.c` (no clicks on hidden tiles except
in the military view), `game/tick.c`, `game/file.c` and `game/file_editor.c` (clear), `game/file_io.c`
(save piece). Revealing: citizens 5 tiles, soldiers 9, the player's buildings 3 + size/2, the start 14.

## Developer tools

Build as usual from `build/` (Ninja). The preview CLI needs `-DBUILD_WORLDGEN_PREVIEW=ON`.

**Quick PNG of the world model** (milliseconds, no game data needed). Also prints the start, entry,
exit, road length, ramp count and whether the river trade route is navigable:

```
./build/worldgen_preview --seed 42 --size 160 --template river_valley --water 50 --relief 50 --forest 50 --scale 4 --out world.png
```

**Real in-game render** with the game's graphics (needs the Caesar III data in `~/Games/caesar3`, opens
a window for a few seconds). Writes `OUT_full.png` (whole city at 100% zoom) and `OUT_minimap.png`:

```
tools/worldgen_preview/render_in_game.sh ~/Games/caesar3 "seed=42,template=coastal,size=160,climate=0" OUT
```

`--worldgen-render` keys, comma separated: `seed`, `size`, `template`, `climate` (0 central,
1 northern, 2 desert), `water`, `relief`, `forest`, plus:

| Key | Effect |
|---|---|
| `fog=0` | Render without fog (use for comparisons) |
| `setup=1` / `setup=2` | Screenshot the setup window / the main menu instead |
| `simulate=TICKS` | Place 12 house plots near the start and run the simulation |
| `savetest=1` | Save and reload, to check the fog save piece |
| `load=NAME` | Load an existing scenario (e.g. an original map) and log its `MAPSTATS` |

Generated worlds also log `MAPSTATS generated`. Run the game from a terminal and grep `MAPSTATS`.

### Checks before committing generator changes

- Every template × 30 seeds × sizes 160 and 512 gets a start, entry, exit and road (was 300/300).
- River trade routes navigable in every seed for river_valley, lakes and highlands, 30/30 for
  coastal and island (was 360/360). Any change to water code must keep this.
- `MAPSTATS` within the range of the hand-made maps: cliffs 0.2-2.5%, level-0 land 94%+, water 7-22%
  in 2-8 bodies, trees 4-26%, rock 2-3%, meadow 1-4%.
- Look at real in-game renders next to an original map before calling terrain good.

## Gotchas

- After building a map through the editor path, call `image_load_climate(climate, 0, 1, 0)` before
  showing the city, or it renders black (editor graphics are still loaded).
- Tree and shrub images are not recomputed on scenario load; the exporter updates them.
- Render determinism needs the `random` grid set from the world (`map_random_set`).
- The game hangs on a folder picker if not given the data directory.
- Boats need deep water with all 4 neighbours water: channels at least 3 wide. A river that meanders
  off the map and back in breaks the trade route; rivers are cut at the first sample outside the map.
- Stage RNG stream numbers in `stages.h` are fixed: changing one changes every world.
- Save version: this fork uses `0xc1` (`SAVE_GAME_LAST_NO_FOG_OF_WAR = 0xc0`). This will collide with
  upstream's next version bump when merging.

## Roadmap

Next, in rough order (owner has not picked yet):

1. Terrain "soul": signature landmarks; livelier highlands.
2. Resource deposits: mode-only layer; mines and quarries need the matching deposit nearby.
3. Points and natives: invasion, herd and fishing points; native villages.
4. Validation with deterministic retry (dock, wharf, essential resources near the start).
5. Grid widening to 512/1024: coordinates are `unsigned char` and grid offsets `short` in buildings
   and figures, plus a new save version. Profile routing on a busy large city before offering 1024.
6. Pre-game economy and start settings (funds, difficulty, allowed industries).
7. Generated empire map and trade (reuse the empire XML import); generated events (reuse the
   scenario event system).

## Polish list

- Fog edges are jagged; multi-tile buildings can show slivers at the fog edge.
- Weather is drawn over fog; the save-game preview ignores fog.
- Setup window strings are English only; the seed cannot be typed.
- Small road loops where the road in and the road out overlap.

## Prior art

Keriew/augustus#1723 by bacox: an upstream map generator PR (AGPL; credit if code is borrowed).
Feature request Keriew/augustus#1557.
