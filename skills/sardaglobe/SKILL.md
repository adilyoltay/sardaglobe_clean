---
name: sardaglobe
description: Google Earth-class native C++/OpenGL globe engine — SSE LOD, 8-state tile lifecycle with cancel, async DEM mesh bake, shader-level unpop/crossfade, terrain-aware camera, 3-stage frame pipeline. 35 CTest green.
---

# SardaGlobe

## Mission

Build a **Google Earth-class globe engine** in native C++/OpenGL.
Sole parity target: **Google Earth** (WASM + Desktop RE based).

## Core Principles

1. **Google Earth is the sole parity target** — All behavior, architecture, and UX decisions reference GE.
2. **Minimal changes** — Small, tested patches over rewrites.
3. **Always shippable** — Every commit must pass `ctest` (currently **35/35 green**).
4. **Evidence-driven** — Changes reference GE RE section numbers; no speculative features.

## Architecture Overview

```
┌─────────────────── GlobeEngine (src/engine/) ───────────────────┐
│  ProcessInput → Update (BuildNextScene) → Render (SceneSnapshot) │
├──────────────┬──────────────┬──────────────┬────────────────────┤
│ TilePyramid  │ TileScheduler│ DemManager   │ TileRenderer       │
│ LodSelector  │ TileFetcher  │ HeightmapMgr │ RenderFrame        │
│ TileStateMach│ TileDecoder  │ MeshScheduler│ ShaderManager      │
│ (scheduling/)│ (io/)        │ (io/+render/)│ TextureManager     │
│              │ TileCache    │              │ TextureAtlas       │
│              │ MemoryCache  │              │ MeshTemplate       │
│              │ DecodedCache │              │ CornerLod          │
├──────────────┴──────────────┴──────────────┴────────────────────┤
│ EarthCamera + FlightController (src/camera/)                     │
└──────────────────────────────────────────────────────────────────┘
```

## GE Parity Status

| Target | GE Ref | Status | Implementation |
|--------|--------|--------|----------------|
| 3-stage frame (DoFrame → BuildNextScene → Render) | §2 | ✅ | `SceneSnapshot` in `globe_engine.h` |
| SSE-based LOD + neighbor conformance | §4.1 | ✅ | `LodSelector`, `LodConformanceTest` |
| `uCornerLods` bilinear LOD interpolation | §4.2 | ✅ | `corner_lod.h`, shader uniform, `CornerLodTest` |
| Tile state machine (8 states, 12 events) | §3.3 | ✅ | `tile_state_machine.h` — includes `Canceled`+`Cancel` |
| Unpop + parent-child raster crossfade | §3.4-3.5 | ✅ | Shader: `uUnpopBlend`+`uTexScaleOffsetUnpop`+`uRasterCrossfade` |
| Terrain morph (flat→DEM) | §3.6 | ✅ | `uTerrainMorph`, 200ms blend |
| Async DEM fetch + batch + priority | §5 | ✅ | `DemManager` with LRU, pin, health, co-eviction |
| CPU mesh bake (DEM→vertex displacement) | §5+§15 | ✅ | `TileMeshBuilder` + `TileMeshScheduler` (worker pool) |
| Worker pool fetch + decode + mesh | §7 | ✅ | `TileFetcher(16)` + `TileDecoder(8)` + `TileMeshScheduler(4)` |
| 4-layer cache (GPU→Decoded→Memory→Disk→Net) | §8 | 🟡 | 5 layers exist; promotion/invalidation tuning partial |
| Viewport-out cancel + touch-based lifecycle | §8.3 | ✅ | `cancelAfterFramesUntouched`, `Event::Cancel` |
| DEM/raster co-eviction | §8 | ✅ | `DemManager::UnpinAndEvict`, callback in eviction |
| Skirt generation (selective per edge) | §4.3 | ✅ | `MeshTemplate` shared EBO, `selectiveSkirts` config |
| Predictive view prefetch | GE Pro RE | ✅ | Momentum-projected 1-2s lookahead |
| Near-camera render sort | GE Pro RE | ✅ | Front-to-back leaf+fallback sort |
| Terrain-aware camera (orbit/pan/zoom) | Nav RE | ✅ | `EarthCamera` + `FlightController` |
| Request-driven frame (idle sleep) | §10 | ✅ | `requestDrivenFrame` config |
| Log-depth / Reversed-Z precision | §6 | 🟡 | Both paths active; visual z-fighting gate pending |
| Texture atlas + instanced draw | §8 | ✅ | `TextureAtlasAllocator` + `DrawElementsInstanced` |
| Adaptive resource limits | — | 🟡 | Config flag `adaptiveResourceLimits` (off by default) |
| Atmosphere / sky pass | §6.5 | ❌ | Not implemented |
| Label rasterization | §11.4 | ❌ | Not implemented |
| Vector tile render | §11 | ❌ | `LayerManager` exists but not integrated |
| Depth plane equations | §4 | ❌ | Pending parity gate evidence |

> Section references (§) point to `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md`.

## Module Map

| Module | Directory | Key Files |
|--------|-----------|-----------|
| **Engine** | `src/engine/` | `globe_engine.h/.cpp` — main loop, frame pipeline, tile orchestration |
| **Scheduling** | `src/scheduling/` | `tile_pyramid.h`, `lod_selector.h`, `tile_scheduler.h`, `tile_state_machine.h`, `job_system.h` |
| **IO/Data** | `src/io/` | `dem_manager.h`, `tile_fetcher.h`, `tile_decoder.h`, `tile_cache.h`, `memory_tile_cache.h` |
| **Rendering** | `src/rendering/` | `tile_renderer.h`, `render_frame.h`, `shader_manager.h`, `texture_manager.h`, `tile_mesh_scheduler.h`, `tile_mesh_builder.h`, `mesh_template.h`, `corner_lod.h`, `heightmap_manager.h`, `texture_atlas_allocator.h` |
| **Camera** | `src/camera/` | `earth_camera.h/.cpp`, `flight_controller.h/.cpp` |
| **Core** | `src/core/` | `tile.h`, `tile_key.h`, `config.h`, `constants.h`, `bounded_queue.h`, `frame_time_tracker.h`, `ellipsoid.h`, `extent.h` |
| **Math** | `src/math/` | `frustum.h`, `tile_math.h` |
| **Debug** | `src/debug/` | `network_panel.h/.cpp` — HTTP request tracing |
| **Tests** | `tests/` | 37 test files, 35 CTest targets |

## Tile State Machine

```
Unloaded ──Schedule──→ Scheduled ──FetchStart──→ Fetching ──FetchOk──→ Decoding
    ↑                      │              │              │              │
    │                      ├──Cancel──→ Canceled ←──Cancel──┤      ──Cancel──→ Canceled
    │                      │                   │                          │
    │                  FetchFail            Schedule                   DecodeOk
    │                      ↓                   ↓                          ↓
    │                   Failed ←──DecodeFail── Scheduled            Uploading
    │                      │                                           │
    │                   Schedule                                  UploadOk
    │                      ↓                                          ↓
    │                   Scheduled                                   Ready
    │                                                                 │
    └──────────────────── Evict (any state) ──────────────────────────┘
```

8 states: `Unloaded`, `Scheduled`, `Fetching`, `Decoding`, `Uploading`, `Canceled`, `Ready`, `Failed`
12 events: `Schedule`, `FetchStart`, `FetchOk`, `FetchFail`, `DecodeStart`, `DecodeOk`, `DecodeFail`, `UploadStart`, `UploadOk`, `Cancel`, `Evict`, `Drop`

## Key Constants (src/core/constants.h)

| Constant | Value | Source |
|----------|-------|--------|
| `EARTH_RADIUS_KM` | 6378.137 | WGS84 [binary] |
| `TILE_SIZE_PX` | 256 | GE standard [binary] |
| `MAX_ZOOM` | 22 | GE standard [binary] |
| `DEFAULT_SSE_THRESHOLD` | 1.4 | Tuned [inference] |
| `MAX_CONCURRENT_FETCHES` | 16 | GE WASM [inference] |
| `MAX_CONCURRENT_DECODES` | 8 | GE WASM [inference] |
| `TEXTURE_UPLOAD_BUDGET_MS` | 2.0 | GE WASM [inference] |
| `MESH_SCHEDULER_WORKERS` | 4 | Config default |

## Build & Run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure   # 35/35 green
./build/native_globe                          # Run engine
```

## Workflow

1. **Reference** — Check GE behavior in RE docs (section §).
2. **Implement** — Minimal C++ change in the correct subsystem.
3. **Test** — `ctest` must stay green. Add regression test for parity-affecting changes.
4. **Document** — Update `CORE_GLOBE_PARITY_PLAN.md` status table.

## Key Docs

| Document | Purpose |
|----------|---------|
| `AGENTS.md` | Project rules, sole parity target, document/reference index |
| `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md` | **Main GE RE technical reference** (WASM RE + Terrain Plan + Pipeline Optimization) |
| `docs/GOOGLE_EARTH_MOUSE_NAVIGATION_ANALYSIS.md` | GE navigation RE (camera, orbit, zoom, momentum) |
| `docs/CORE_GLOBE_PARITY_PLAN.md` | **Live parity tracker** — code-validated status of all subsystems |

## Open Parity Gaps (Priority Order)

1. **Visual parity acceptance gate** — Screenshot-diff CI pipeline for z-fighting, seam, pop regression
2. **Cache hierarchy tuning** — GPU→Memory promotion/invalidation not at GE level
3. **Depth plane equations** — Conditional on parity gate evidence (log-depth may suffice)
4. **Atmosphere/sky** — No render pass
5. **Label/text rasterization** — No pipeline
6. **Vector tile integration** — `LayerManager` stub only
