---
name: sardaglobe
description: Google Earth-class native globe engine — tile pyramid, SSE LOD, async DEM, terrain-aware camera, mirth-inspired render pipeline.
---

# SardaGlobe

## Mission

Build a **Google Earth-class globe engine** in native C++/OpenGL:
- **GE Parity** — Sole reference is Google Earth (WASM RE based)
- **Modern** — Clean C++ architecture, GPU-first rendering, worker-based pipeline
- **Stable** — No visual artifacts, pop-free tile transitions, smooth LOD
- **Fast** — 60fps on mid-range hardware, async everything, budgeted uploads
- **Intuitive** — Terrain-aware orbit/pan/zoom, momentum, smooth animations

## Core Principles

1. **Google Earth is the sole parity target** — All behavior, architecture, and UX decisions reference GE.
2. **Minimal changes** — Small, tested patches over rewrites.
3. **Always shippable** — Every commit should be production-ready.

## GE Architecture Targets

| Target | GE Reference | Status |
|--------|-------------|--------|
| 3-phase frame loop (DoFrame → BuildNextScene → Render) | §2, §10.3 | Planned |
| SSE-based LOD with `uCornerLods` bilinear interpolation | §4.2 | Partial |
| Tile state machine (UNLOADED → READY → EVICTED) | §3.3 | Partial |
| Async DEM (BatchGetElevationsByPoint → mesh) | §5 | FAZ 0-3 ✅ |
| Unpop/Crossfade (uUnpopBlend, RASTER_CROSSFADE) | §3.4, §3.5 | Planned |
| Worker pool decode + async mesh build | §7 | Planned (P5) |
| 4-layer cache (GPU → Memory → Disk → Network) | §8 | Partial |
| Terrain-aware camera (orbit pivot, zoom-to-cursor) | Nav RE | FAZ 2 ✅ |
| Skirt generation for LOD seam prevention | §4.3 | Partial |

> Section references (§) point to `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md` BÖLÜM A.

## Quick Reference

| Area | GE RE Section | C++ Location |
|------|---------------|--------------|
| Navigation & Camera | Nav Analysis doc | `src/camera/` |
| Tile/LOD/Scheduling | Deep Analysis §3-4 | `src/core/`, `src/engine/`, `src/scheduling/` |
| DEM/Elevation | Deep Analysis §5 | `src/io/dem_manager.*` |
| Rendering/Shaders | Deep Analysis §6 | `src/rendering/` |
| Threading/Jobs | Deep Analysis §7 | `src/core/job_system.*` |

## Workflow

1. **Understand** — Check Google Earth behavior via WASM RE docs.
2. **Implement** — Minimal C++ change in the right subsystem.
3. **Test** — Run existing tests, add if needed.

## Key Docs

- `AGENTS.md` — Project rules, sole parity target (Google Earth), document index
- `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md` — **Main technical reference** (GE WASM RE + 3D Terrain Plan + Tile Pipeline Optimization)
- `docs/GOOGLE_EARTH_MOUSE_NAVIGATION_ANALYSIS.md` — GE navigation RE (camera, orbit, zoom, momentum)
- `docs/MASTER_DEVELOPMENT_PLAN.md` — 7-phase development roadmap
