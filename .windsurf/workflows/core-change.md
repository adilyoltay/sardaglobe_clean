---
description: Workflow for core engine changes (LOD, tile pyramid, DEM, scheduling) in Native Globe
---

# Core Change Workflow

## Steps

1. **Identify** parity target from `AGENTS.md` (sole target: Google Earth)
2. **Locate** GE behavior in `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md`
4. **Map** to C++ file:
   - LOD/pyramid → `src/scheduling/tile_pyramid.*`
   - Scheduling → `src/scheduling/tile_scheduler.*`
   - Engine → `src/engine/globe_engine.*`
   - DEM → `src/io/dem_manager.*`
5. **Define** invariants before coding
6. **Implement** minimal parity-first change
7. **Test** with `tests/lod_conformance_test.cpp`
8. **Validate** quality gates
9. **Update** docs if phase completes
