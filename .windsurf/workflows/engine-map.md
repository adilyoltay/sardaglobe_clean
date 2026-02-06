---
description: Core engine dosya haritası ve modül referansı
---

# Core Engine Map

GE referans: `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md`

## 1. Tile Pyramid ve LOD Selection (GE §3-4)
- `src/scheduling/tile_pyramid.h/.cpp` — Quadtree traversal, tile selection
- `src/scheduling/lod_selector.h/.cpp` — SSE-based LOD karar
- `src/core/tile.h` — Tile struct, TileKey
- `src/core/tile_key.h` — QuadKey, parent/child/neighbor

## 2. Tile Scheduling ve Jobs (GE §3.2, §7)
- `src/scheduling/tile_scheduler.h/.cpp` — Request dispatch, state machine
- `src/scheduling/job_system.cpp` — Job submit/process

## 3. IO Pipeline: Fetch / Decode / Cache (GE §3.2, §8)
- `src/io/tile_fetcher.h/.cpp` — HTTP fetch, CURL pooling
- `src/io/tile_decoder.h/.cpp` — Image decode (worker thread)
- `src/io/tile_cache.h/.cpp` — Disk cache
- `src/io/tile_url_template.h/.cpp` — URL template parser
- `src/io/download_types.h` — FetchRequest, FetchResult

## 4. DEM / Elevation (GE §5)
- `src/io/dem_manager.h/.cpp` — DEM fetch, priority queue, cache

## 5. Engine Control (GE §2, §10.3)
- `src/engine/globe_engine.h/.cpp` — Frame loop, Update, Render

## 6. Camera ve Navigation (Nav RE)
- `src/camera/earth_camera.h/.cpp` — Camera state, projection
- `src/camera/flight_controller.h/.cpp` — Orbit, pan, zoom, tilt, momentum

## 7. Rendering Pipeline (GE §6)
- `src/rendering/tile_renderer.h/.cpp` — Tile draw, batch
- `src/rendering/render_frame.h/.cpp` — Frame setup, passes
- `src/rendering/shader_manager.h/.cpp` — Shader compile/uniform
- `src/rendering/texture_manager.h/.cpp` — Texture upload, eviction
- `src/rendering/tile_mesh_builder.h/.cpp` — Mesh generation (vertices, skirts)
- `src/rendering/tile_mesh_scheduler.h/.cpp` — Async mesh worker pool
- `src/rendering/mesh_template.h/.cpp` — Shared EBO singleton
- `src/rendering/heightmap_manager.h/.cpp` — DEM heightmap GPU

## 8. Core Types
- `src/core/config.h` — Runtime config
- `src/core/constants.h` — Globe radius, math constants
- `src/core/extent.h` — Geographic extent
- `src/core/lonlat.h` — Lon/Lat type
- `src/core/ellipsoid.h/.cpp` — WGS84 ellipsoid
- `src/core/bounded_queue.h` — Thread-safe bounded queue
- `src/core/frame_time_tracker.h` — Frame timing telemetry
- `src/math/frustum.h` — Frustum culling
- `src/math/tile_math.h` — Tile math utilities

## 9. Debug
- `src/debug/network_panel.h/.cpp` — Network debug panel

## 10. Tests
- `tests/lod_conformance_test.cpp` — Numeric parity
- `tests/visual_lod_test.cpp` — Visual regression
- `tests/globe_system_test.cpp` — System integration
- `tests/edge_mask_test.cpp` — Edge mask logic
