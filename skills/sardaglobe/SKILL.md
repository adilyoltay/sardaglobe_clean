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

1. **Parity first** — Match `globe-web-html/libs/webglobe.js` API/behavior before optimizing.
2. **Navigation exception** — Mouse/keyboard follows Google Earth feel, not JS.
3. **Minimal changes** — Small, tested patches over rewrites.
4. **Always shippable** — Every commit should be production-ready.

## Quick Reference

| Area | Source | C++ Location |
|------|--------|--------------|
| API behavior | `globe-web-html/libs/webglobe.js` | `src/api/` |
| Navigation | Google Earth (reversed) | `src/camera/` |
| Tile/LOD | `webglobe_deobfuscated_v2/` | `src/core/`, `src/engine/` |
| Rendering | Parity + perf | `src/rendering/` |

## Workflow

1. **Understand** — Check JS behavior or Google Earth for navigation.
2. **Implement** — Minimal C++ change in the right subsystem.
3. **Test** — Run existing tests, add if needed.
4. **Document** — Update `docs/API_PORT_REVIEW_PROMPT.md` on phase completion.

## Key Docs

- `AGENTS.md` — Project index and rules
- `docs/API_PORT_REVIEW_PROMPT.md` — Phase tracking
- `docs/GOOGLE_EARTH_INTEGRATION_REPORT.md` — Reverse-engineered insights
