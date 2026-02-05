---
description: SardaGlobe core engine parity-first development workflow
---

# SardaGlobe Core Engine Workflow

Bu workflow, globe engine core değişikliklerinde (tile pyramid, SSE LOD, async DEM) parity-first yaklaşımı uygular.

## Temel Kurallar

1. **Parity first** — `globe-web-html/libs/webglobe.js` API/behavior ile eşleşmeli.
2. **Navigation exception** — Mouse/keyboard için Google Earth davranışı esas alınır, JS değil.
3. **Minimal changes** — Küçük, test edilmiş patch'ler, büyük rewrite'lar yerine.
4. **Always shippable** — Her commit production-ready olmalı.

## Workflow Adımları

1. **Önce AGENTS.md'yi oku** — Proje index ve kuralları için.
2. **docs/API_PORT_REVIEW_PROMPT.md kontrol et** — Phase tracking ve güncel durum.
3. **Kaynak analizi yap:**
   - API davranışı için: `globe-web-html/libs/webglobe.js`
   - Navigasyon için: Google Earth reversed (`docs/GOOGLE_EARTH_INTEGRATION_REPORT.md`)
   - Tile/LOD için: `webglobe_deobfuscated_v2/`
4. **C++ değişikliğini doğru subsystem'de yap:**
   - API → `src/api/`
   - Camera/Navigation → `src/camera/`
   - Tile/LOD → `src/core/`, `src/engine/`, `src/scheduling/`
   - Rendering → `src/rendering/`
5. **Test et** — `tests/lod_conformance_test.cpp`, `tests/visual_lod_test.cpp`
6. **Faz tamamlandıysa** — `docs/API_PORT_REVIEW_PROMPT.md` güncelle.

## Quality Gates

- [ ] JS parity kontrol edildi (navigation exception hariç)
- [ ] LOD/SSE monotonic ve stable
- [ ] Tile state machine stuck state yok
- [ ] DEM async, render bloklanmıyor
- [ ] Görsel seam/popping yok
- [ ] Testler geçiyor
