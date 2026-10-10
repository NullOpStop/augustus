# Frontier mode: world generation design

Status: draft, 2026-10-10. Living document for the `frontier-mode` branch.

## Goal

Generate a whole, coherent, playable world before the game starts. The player begins at a single
settlement surrounded by fog of war and explores outward to find land, water and resources.
Settings chosen on a pre-game screen (size, seed, climate, terrain, richness, economy) feed the
generator directly.

We generate the entire world up front rather than lazily, so the generator can reason about the
world as a whole: rivers that flow downhill to the sea, resources placed for gameplay, a start site
that is chosen rather than random, and validation that rejects broken worlds.

## What the engine actually needs

These are hard facts from the current code that shape every design decision below.

### What a generator writes

Only a few per-tile grids are source data; everything else is derived by the engine.

| Grid | Source | Notes |
|---|---|---|
| terrain flags (`map/terrain.h`) | yes | TREE, ROCK, WATER, SHALLOW_WATER, SHRUB, MEADOW, MARSHLAND, ELEVATION (slope tile), ACCESS_RAMP, ROAD, BUILDING |
| elevation (`map/elevation.c`) | yes | 0..5, orthogonal neighbours may differ by at most 1 (`map_elevation_remove_cliffs`) |
| random (`map/random.c`) | yes | picks image variants; `map_random_init()` fills it |
| bitfields alternate-terrain bit | yes | grass variety; `map_property_init_alternate_terrain()` |
| images, edges, routing, desirability… | no | rebuilt by `map_tiles_update_all()` and friends |

Gotchas:
- Tree and shrub images are **not** recomputed when a scenario is loaded; when generating in memory
  run `map_tiles_update_all()` (which includes trees/shrubs) after `map_image_clear()` and
  `map_image_context_init()`.
- Any water shape gets a valid image, so water bodies can be arbitrary shapes.
- Slope tiles (`TERRAIN_ELEVATION`) are impassable and unbuildable; plateau tops are buildable but
  are cut off unless a 2x2 access ramp connects them.
- Image lookups do not bounds-check; the map must keep at least one ring of `TERRAIN_MAP_EDGE` tiles.

### What the economy needs from terrain

There is **no fertility** and **no ore type** in terrain. Every dependency is "is there a tile with
flag X within radius r of the building":

| Building | Requirement |
|---|---|
| All farms (wheat, veg, fruit, olives, vines, pigs) | MEADOW within 1 of the 3x3 footprint |
| Marble quarry, iron mine, gold mine, stone quarry | ROCK within 1 (any rock, any mine) |
| Timber yard | TREE or SHRUB within 1 |
| Clay pit | WATER within 3 |
| Sand pit | WATER within 9 |
| Reservoir (self-filling) | WATER within the 5x5 around it |
| Wharf / shipyard / dock | one footprint edge on water, 3 full rows of water in front |
| Dock | adjacent water must be boat-reachable from the river entry |
| Shipyard launching boats | an adjacent water tile with all 8 neighbours water |
| Lighthouse | boat-reachable water within 9 |

Climate is almost cosmetic: graphics, herd animal (sheep/wolves/zebras), wheat halved in northern,
fountain range -1 in desert, camels in desert.

### Water, boats and bridges

- Sea, river and lake are all just `TERRAIN_WATER`. "Open water" means boat-reachable from the
  river entry point.
- A tile is boat-passable only if it is deep water (not shallow, not marsh) and all 4 orthogonal
  neighbours are water. **A navigable channel must be at least 3 tiles wide.**
- River entry/exit points are map-edge water tiles with 4 water neighbours. Trade ships spawn there
  and need a working dock.
- Bridges are straight, at most ~65 tiles, need straight shorelines at both ends (end tiles have
  exactly 3 water neighbours), and cannot touch marsh. Ship bridges need length >= 5.

### Connectivity and points

- Entry to exit must be connected over walkable land (4-directional). If not, the game pauses with
  the "road to Rome" warning. Natural terrain cannot be cleared, so the generator must guarantee it.
- Invasion points: clear map-edge tiles; an invasion is cancelled if its spawn tile is rock, tree,
  slope or water. Enemies cannot cross trees, rock, water or slopes.
- Herd points: clear tiles with open grass/meadow around them (animals roam within 4 tiles).
- Fishing points: boat-passable water in the same water body as the player's wharves (x > 0).
- Natives: in memory, create with `building_create` + `map_building_tiles_add`, as the editor does.

### Size limits

`GRID_SIZE` is 162, the playable area at most 160x160. Buildings and figures store coordinates as
`unsigned char` and tile offsets as `short`, so going larger (512 default, 1024 optional) requires
widening those types and a new save version. That is a separate track from the generator.

## Architecture

The generator does **not** write Augustus grids directly. It builds its own world model at any size,
then an exporter stamps that model into the engine.

```
settings ──► generator ──► world model ──► exporter ──► Augustus grids + scenario
                │               │
                │               └──► CLI preview (PNG) for fast iteration
                └── seeded RNG streams, noise
```

Why:
- The generator is independent of `GRID_SIZE`, so it can be developed now at <= 160 and scale up
  later without changes.
- A small command-line tool can generate a world and write a PNG in milliseconds, so terrain can be
  tuned without launching the game.
- Validation and tests run on the world model, not on game state.

Proposed layout (all new code, nothing in existing modules except the exporter's calls into them):

```
src/frontier/worldgen/
    rng.c/.h            seeded PCG32, one sub-stream per stage
    noise.c/.h          2D simplex noise, fbm, domain warp
    world.c/.h          world model: per-cell height, water, surface, deposit, flags
    gen_shape.c         macro template, heightfield, terracing
    gen_water.c         sea, lakes, rivers, shallows, marsh
    gen_surface.c       meadow, grass, shrub, forest, rock outcrops
    gen_resources.c     deposits and resource balance
    gen_sites.c         start site, entry/exit road, points, natives
    validate.c          playability checks
    export.c            world model -> Augustus terrain/elevation/scenario
tools/worldgen_preview/ CLI: seed + settings -> PNG
```

### Determinism

- Own PRNG (PCG32 or splitmix64), never `rand()` or the game's global RNG, so a seed produces the
  same world on every platform.
- Each stage draws from its own stream derived from `(seed, stage_id)`. Tweaking forests then does
  not reshuffle rivers.
- Validation retries derive a new sub-seed deterministically, so the visible seed still reproduces.

## Pipeline

### 0. Settings

| Setting | Effect |
|---|---|
| World size | 160 now; 512 default and 1024 optional once the grid is widened |
| Seed | everything |
| Climate | central / northern / desert |
| Template | river valley, coastal, lakes, highlands, island, random |
| Water | sea level, river count |
| Relief | how much hill/plateau land |
| Forest | tree coverage |
| Resource richness | deposit count and size, how far luxuries sit from the start |
| Natives | none / few / many |
| Economy & start | funds, difficulty, allowed industries (feeds the scenario, not terrain) |

### 1. Macro shape

Pick a template that decides the big picture before any noise: where the coast is, where the main
river runs, where the highlands sit. Templates give each world a readable identity and guarantee
variety. Noise then adds detail inside that frame.

### 2. Height

- Continuous heightfield from fbm simplex noise plus template bias, with domain warp for natural
  shapes.
- Quantize to engine levels 0..5 (most land 0..2, highlands up to 5).
- Enforce the one-step rule and clean up single-tile pits and spikes.
- Find every plateau (connected region at the same level) and place at least one valid 2x2 access
  ramp to a lower neighbour; isolated plateaus without a ramp site get flattened.

### 3. Water

- Sea along template edges below sea level.
- Rivers: start in highlands, follow the downhill gradient (with meander noise) to the sea or map
  edge. Width from accumulated flow: tributaries 1-2 tiles (not navigable), the main river at least
  3 tiles (navigable) from edge to edge.
- Lakes fill closed depressions.
- Shallow water band along shores; deep water elsewhere so boat routes work.
- Marsh on low, wet, flat land next to water, kept away from planned bridge crossings.

### 4. Surface

Moisture = distance to water + noise. Then classify land:
- Meadow: low, moist river valleys and lake shores (the "fertile" land, since farms need meadow).
- Grass: the default buildable land.
- Shrub and forest: moist uplands and bands between valleys.
- Rock outcrops: on hills and slopes, grouped into 2x2 and 3x3 blocks so the engine uses its large
  rock images.

### 5. Resources

The engine ties every mine to "any rock" and clay/sand to "any water". Frontier mode adds **real
deposits**, a small mode-only mechanic (see decisions):
- Common near the start: meadow, timber, clay, stone.
- Further out: marble in hills, iron in highlands, vines/olives on specific meadow types, gold rare.
- Richness setting scales count, cluster size and distance.

### 6. Start site and the via

- Score candidate sites: buildable flat area, meadow, water (for reservoir and clay), timber and
  stone within reach, away from edges and invasion points, not in marsh.
- Pick a good-but-not-perfect site from the top candidates.
- Lay the via: a road from the entry edge to the start and on to the exit edge, routed with A* over
  the world model. Where it must cross a river, shape the crossing so a straight bridge fits.
- Fog of war starts revealed around the settlement and along the nearby stretch of road.

### 7. Points and natives

- Entry/exit at the via's ends. River entry/exit at the navigable river's ends.
- Invasion points: clear, reachable edge tiles, spread around the world, far from the start.
- Herd points on open grassland away from the start. Fishing points on navigable water.
- Native villages (huts + meeting centre) at mid distance on meadow/forest edges.

### 8. Validation

Reject and retry (deterministically) if any check fails:
- Entry, start and exit connected over walkable land.
- River entry to exit navigable, with at least one valid dock site.
- At least one valid wharf + shipyard site on fishable water.
- Every essential resource within a radius of the start; luxuries exist somewhere.
- Enough buildable flat land at the start.

### 9. Export

Write terrain flags and elevation, init the map edge, random grid and alternate terrain, run the
engine's tile updates, set scenario metadata (size, climate, points, allowed buildings, funds), then
start the game. Starting directly from memory is the goal; writing a temporary `.mapx` and loading
it is an acceptable first step.

## What we take from the upstream map generator PR

Keriew/augustus#1723 by bacox (AGPL, credit in file headers and commits):
- Ideas: generation call order, the minimap preview technique, the river random walk, land
  segmentation by flood fill, building a road by walking the routing distance grid.
- Not reused: its global-`rand()` seeding, its incomplete simplex noise, the per-tile tree update
  hack, its UI window.

## Decisions

1. **Resource deposits: yes.** A mode-only deposit layer: iron, marble, gold and stone live in
   specific places, and mines/quarries in frontier mode require the matching deposit nearby.
   Exploration is how you find them.
2. **The via is long, and that is fine.** Entry and exit sit on the world edge; immigrants and
   traders walk the road to the settlement.
3. **Shaped worlds.** Templates set the big picture, noise adds detail.
4. **Use the full elevation range (0..5) and make it dramatic.** See below.
5. **Development size.** Build and tune at <= 160 while grid widening proceeds separately.

## Elevation design

The engine supports six levels (0..5); hand-made maps rarely use more than one or two. Frontier
worlds use all of them, as broad terraces rather than noise speckle:

- **Lowlands (0):** sea, rivers, lakes, marsh and the fertile meadow valleys. Water is always level 0.
- **Rolling hills (1-2):** wide terraces, plenty of buildable land, stone and marble outcrops.
- **Highlands (3-4):** stepped plateaus with forest and rock; iron lives here.
- **Peaks (5):** small summits with the rarest deposits (gold).

Rules that make it work in the engine:
- Each terrace is a broad, connected plateau (minimum area, minimum width so a ramp fits); tiny
  remnants are merged into a neighbour level.
- Each step is one level, so climbing 0 to 4 means passing through every terrace. Every plateau gets
  at least one 2x2 access ramp, placed so the climb from the start reads like a route: switchbacks
  up the highlands.
- Rivers stay at level 0 and cut through higher ground, leaving gorges with cliff walls on both banks.
- Mesas: isolated raised plateaus with a single ramp. Slopes are impassable to enemies, so these are
  natural fortress sites.
- The start area is kept mostly at levels 0-1 so early building is not a puzzle.
- Valuable deposits sit higher up: climbing is rewarded.

## Build order

1. World model, RNG, noise and the PNG preview tool.
2. Height and water, tuned visually in the preview tool.
3. Surface and resources.
4. Start site, via, points, validation.
5. Exporter and starting a game from it (still at <= 160).
6. Pre-game settings window.
7. Fog of war (separate design).
8. Grid widening to 512/1024 (separate design).
