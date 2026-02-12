# Core Engine Map

## 1. Engine Control Surface
- `src/engine/globe_engine.h/.cpp` — Main loop, frame pipeline (ProcessInput→Update→Render), tile orchestration, viewport-out cancel, DEM co-eviction callback, adaptive limits

## 2. Tile Pyramid and LOD Selection
- `src/scheduling/tile_pyramid.h/.cpp` — Centralized LOD selection, ranked required/prefetch sets, center bias scoring
- `src/scheduling/lod_selector.h/.cpp` — SSE traversal, frustum+horizon culling, neighbor conformance, predictive prefetch

## 3. Tile Scheduling and State Transitions
- `src/scheduling/tile_scheduler.h/.cpp` — Fetch→decode→upload lifecycle, cancel flow, pending tracking, cache layer coordination
- `src/scheduling/tile_state_machine.h/.cpp` — 8 states / 12 events, `Canceled`+`Cancel`, transition validation
- `src/scheduling/job_system.h/.cpp` — Generic async job dispatch (not yet in production use)

## 4. IO and Data Pipeline
- `src/io/tile_fetcher.h/.cpp` — HTTP worker pool (N=16), CURL thread-local reuse, cancel via progress callback
- `src/io/tile_decoder.h/.cpp` — Image decode worker pool (N=8), urgent/normal fairness
- `src/io/tile_cache.h/.cpp` — Disk cache (read/write/remove)
- `src/io/memory_tile_cache.h/.cpp` — LRU compressed tile memory cache
- `src/io/decoded_tile_blob.h/.cpp` — Pre-decoded RGBA blob cache (decode bypass)
- `src/io/tile_url_template.h/.cpp` — Regex-free URL template parser
- `src/io/download_types.h` — `FetchRequest`, `FetchResult`, `DecodeRequest`, `DecodeResult`, `Priority`

## 5. Elevation and DEM
- `src/io/dem_manager.h/.cpp` — Batch DEM fetch, LRU cache with pin/evict, health check, auth backoff, `UnpinAndEvict` for co-eviction, parent fallback sampling

## 6. Camera and Navigation
- `src/camera/earth_camera.h/.cpp` — WGS84 perspective camera, terrain-aware near/far, LookAt/FlyTo
- `src/camera/flight_controller.h/.cpp` — Mouse orbit/pan/zoom/tilt, momentum, double-click fly-to

## 7. Rendering Pipeline
- `src/rendering/tile_renderer.h/.cpp` — Batch render, crossfade, heightmap displacement, instanced flat-tile path
- `src/rendering/render_frame.h/.cpp` — 3-pass gap-free render (fallback→leaf→crossfade), front-to-back sort
- `src/rendering/shader_manager.h/.cpp` — Globe shader with `uCornerLods`, `uUnpopBlend`, `uTerrainMorph`, log-depth
- `src/rendering/texture_manager.h/.cpp` — GPU texture upload, atlas integration, pin/evict, eviction callback
- `src/rendering/texture_atlas_allocator.h/.cpp` — Page/slot atlas allocation, defrag/compaction
- `src/rendering/tile_mesh_scheduler.h/.cpp` — Async CPU mesh build worker pool (N=4), priority queue
- `src/rendering/tile_mesh_builder.h/.cpp` — DEM sampling, vertex displacement, normal computation, skirts
- `src/rendering/mesh_template.h/.cpp` — Shared index buffers (EBO) with stitch+skirt mask variants
- `src/rendering/corner_lod.h` — Per-corner LOD weights from edge mask for bilinear interpolation
- `src/rendering/heightmap_manager.h/.cpp` — GPU heightmap texture cache for vertex displacement mode

## 8. Core Types
- `src/core/tile.h` — `TileState` enum (8 states), `Tile` struct (texture, mesh, DEM, fade, morph, priority)
- `src/core/tile_key.h` — QuadKey with parent/child/neighbor/wrap navigation
- `src/core/config.h` — All engine configuration (resource limits, feature flags, DEM settings)
- `src/core/constants.h` — WGS84 params, tile system constants, resource limit defaults
- `src/core/bounded_queue.h` — Thread-safe bounded queue for producer-consumer pipelines
- `src/core/frame_time_tracker.h` — Ring buffer frame timing with P95/P99 percentiles
- `src/core/ellipsoid.h/.cpp` — WGS84 ellipsoid math (geodetic↔ECEF, surface normal)
- `src/core/extent.h` — Geographic tile extent (lon/lat bounds)
- `src/core/layer.h`, `src/core/layer_manager.h/.cpp` — Layer abstraction (stub, not integrated)

## 9. Math Utilities
- `src/math/frustum.h` — Frustum plane extraction from MVP matrix
- `src/math/tile_math.h` — Tile bounds, SSE computation, geographic math

## 10. Debug
- `src/debug/network_panel.h/.cpp` — HTTP request start/complete tracing for telemetry

## 11. Tests (37 files, 35 CTest targets)
- **Lifecycle:** `tile_state_machine_cancel_test`, `tile_scheduler_cancel_flow_test`, `tile_fetcher_cancel_rerequest_test`
- **LOD:** `lod_conformance_test`, `lod_child_quorum_test`, `lod_leaf_nonempty_test`, `lod_refine_budget_test`
- **DEM:** `dem_co_eviction_test`, `dem_continuity_test`, `dem_fallback_test`, `dem_batch_url_order_test`, `dem_grid_row_order_test`, `dem_edge_equalization_fallback_test`, `dem_effective_level_policy_test`
- **Render:** `corner_lod_test`, `depth_precision_test`, `tile_fade_test`, `tile_terrain_morph_test`, `unpop_crossfade_policy_test`, `edge_mask_test`, `seam_metric_truth_test`
- **Mesh:** `mesh_template_stitch_mask_test`, `selective_skirt_mask_test`, `adaptive_mesh_segments_test`
- **Cache:** `tile_cache_stats_test`, `memory_tile_cache_test`, `decoded_tile_blob_test`, `tile_decoder_blob_fastpath_test`
- **Atlas:** `texture_atlas_allocator_test`, `atlas_gutter_uv_test`
- **Integration:** `globe_system_test`, `predictive_prefetch_test`, `tile_pyramid_child_ready_test`, `frustum_extract_test`, `tile_server_connectivity_test`
- **Visual:** `visual_lod_test`, `visual_parity_presets_test`
