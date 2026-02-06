---
name: sardaglobe
description: Native Globe core engine skill — build the world's most modern, stable, fast, and user-friendly globe system.
---

# Sardaglobe

## Mission

Build a **Google Earth-class globe engine** that is:
- **Modern** — Clean C++ architecture, GPU-first rendering
- **Stable** — Predictable behavior, no visual artifacts
- **Fast** — 60fps on mid-range hardware, instant response
- **User-friendly** — Intuitive navigation, smooth animations

## Core Principles

1. **Google Earth parity** — All behavior, architecture, and UX targets Google Earth as the sole reference.
2. **Minimal changes** — Small, tested patches over rewrites.
3. **Always shippable** — Every commit should be production-ready.

## Quick Reference

| Area | Reference | C++ Location |
|------|-----------|--------------|
| Navigation | Google Earth WASM RE | `src/camera/` |
| Tile/LOD | Google Earth WASM RE | `src/core/`, `src/engine/` |
| Rendering | Google Earth WASM RE | `src/rendering/` |
| API surface | Legacy `webglobe.js` (not parity target) | `src/api/` |

## Workflow

1. **Understand** — Check Google Earth behavior via WASM RE docs.
2. **Implement** — Minimal C++ change in the right subsystem.
3. **Test** — Run existing tests, add if needed.
4. **Document** — Update `docs/API_PORT_REVIEW_PROMPT.md` on phase completion.

## Key Docs

- `AGENTS.md` — Project index, rules, and document navigation
- `docs/API_PORT_REVIEW_PROMPT.md` — API parity checklist and phase tracking
- `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md` — **Main technical reference** (3 parts: GE WASM RE + 3D Terrain Plan + Tile Pipeline Optimization)
- `docs/GOOGLE_EARTH_MOUSE_NAVIGATION_ANALYSIS.md` — GE navigation RE (camera, orbit, zoom, momentum)
- `docs/MASTER_DEVELOPMENT_PLAN.md` — 7-phase development roadmap with refactoring status
