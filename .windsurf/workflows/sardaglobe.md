---
description: SardaGlobe core engine parity-first development workflow
---

# SardaGlobe Core Engine Workflow

Tek parity hedefi: **Google Earth** (`AGENTS.md`)

## Temel Kurallar

1. **Google Earth parity** — Tüm davranış, mimari ve UX kararlarında tek referans.
2. **Minimal changes** — Küçük, test edilmiş patch'ler, büyük rewrite'lar yerine.
3. **Always shippable** — Her commit production-ready olmalı.

## Workflow Adımları

1. **AGENTS.md'yi oku** — Proje kuralları ve doküman indeksi.
2. **GE referansını bul:**
   - Tile/DEM/Render/Threading/Cache: `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md` (BÖLÜM A)
   - Terrain plan: aynı doküman BÖLÜM B (§15)
   - Pipeline plan: aynı doküman BÖLÜM C (§16)
   - Navigasyon: `docs/GOOGLE_EARTH_MOUSE_NAVIGATION_ANALYSIS.md`
   - Raw WASM data: `google_earth/`
3. **C++ değişikliğini doğru subsystem'de yap:**
   - Camera/Navigation → `src/camera/`
   - Tile/LOD → `src/core/`, `src/engine/`, `src/scheduling/`
   - Fetch/Decode/Cache → `src/io/`
   - Rendering/Mesh/Shaders → `src/rendering/`
4. **Test et** — `tests/lod_conformance_test.cpp`, `tests/visual_lod_test.cpp`
5. **Quality gates kontrol et** (`/quality-gates`)

## Quality Gates

- [ ] Google Earth parity kontrol edildi
- [ ] LOD/SSE monotonic ve stable
- [ ] Tile state machine stuck state yok
- [ ] DEM async, render bloklanmıyor
- [ ] Görsel seam/popping yok
- [ ] Testler geçiyor
