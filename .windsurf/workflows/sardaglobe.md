---
description: SardaGlobe core engine parity-first development workflow
---

# SardaGlobe Core Engine Workflow

Bu workflow, globe engine core değişikliklerinde (tile pyramid, SSE LOD, async DEM) Google Earth parity yaklaşımını uygular.

## Temel Kurallar

1. **Google Earth parity** — Tek referans Google Earth. Tüm davranış, mimari ve UX kararlarında.
2. **Minimal changes** — Küçük, test edilmiş patch'ler, büyük rewrite'lar yerine.
3. **Always shippable** — Her commit production-ready olmalı.

## Workflow Adımları

1. **Önce AGENTS.md'yi oku** — Proje index ve kuralları için.
2. **Kaynak analizi yap:**
   - Tile/DEM/Render: `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md`
   - Navigasyon: `docs/GOOGLE_EARTH_MOUSE_NAVIGATION_ANALYSIS.md`
   - Raw WASM data: `google_earth/`
4. **C++ değişikliğini doğru subsystem'de yap:**
   - Camera/Navigation → `src/camera/`
   - Tile/LOD → `src/core/`, `src/engine/`, `src/scheduling/`
   - Rendering → `src/rendering/`
   - API → `src/api/`
5. **Test et** — `tests/lod_conformance_test.cpp`, `tests/visual_lod_test.cpp`

## Quality Gates

- [ ] Google Earth parity kontrol edildi
- [ ] LOD/SSE monotonic ve stable
- [ ] Tile state machine stuck state yok
- [ ] DEM async, render bloklanmıyor
- [ ] Görsel seam/popping yok
- [ ] Testler geçiyor
