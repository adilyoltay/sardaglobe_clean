---
description: Workflow for core engine changes (LOD, tile pyramid, DEM, scheduling) in Native Globe
---

# Core Change Workflow

Tek parity hedefi: **Google Earth** (`AGENTS.md`)

## Steps

1. **Locate** GE behavior in `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md`:
   - Tile/LOD → BÖLÜM A §3-4
   - DEM/Elevation → BÖLÜM A §5
   - Render pipeline → BÖLÜM A §6
   - Threading/Cache → BÖLÜM A §7-8
   - Navigation → `docs/GOOGLE_EARTH_MOUSE_NAVIGATION_ANALYSIS.md`
2. **Map** to C++ file:
   - LOD/pyramid → `src/scheduling/tile_pyramid.*`, `src/scheduling/lod_selector.*`
   - Scheduling → `src/scheduling/tile_scheduler.*`
   - Engine → `src/engine/globe_engine.*`
   - DEM → `src/io/dem_manager.*`
   - Fetch/Decode → `src/io/tile_fetcher.*`, `src/io/tile_decoder.*`
   - Rendering → `src/rendering/tile_renderer.*`, `src/rendering/tile_mesh_builder.*`
   - Camera → `src/camera/earth_camera.*`, `src/camera/flight_controller.*`
3. **Define** invariants before coding
4. **Implement** minimal GE-parity change
5. **Test** with `tests/lod_conformance_test.cpp`, `tests/visual_lod_test.cpp`
6. **Validate** quality gates (`/quality-gates`)
