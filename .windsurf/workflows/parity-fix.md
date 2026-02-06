---
description: Quick workflow for fixing Google Earth parity bugs in Native Globe
---

# Parity Fix Workflow

## Steps

1. **Reproduce** - Identify Google Earth behavior via WASM RE docs or direct observation
2. **Locate** - Find the C++ divergence point
3. **Patch** - Minimal change targeting GE parity
4. **Test** - Add regression test if possible
5. **Record** - Note in `docs/API_PORT_REVIEW_PROMPT.md` if it closes a gap

## Reference

- GE WASM RE: `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md`
- GE Navigation RE: `docs/GOOGLE_EARTH_MOUSE_NAVIGATION_ANALYSIS.md`
- Raw data: `google_earth/` (WASM, WAT, reconstructed headers)
