# AGENTS.md — Native Globe Reference Index

Bu dosya, projedeki ana dokümanları ve kaynak referanslarını tek noktadan listeler.

## Ana Master Kural
**Tek parity hedefi: Google Earth**

Amacımız Google Earth kalitesinde bir globe engine geliştirmektir. Tüm davranış, mimari ve UX kararlarında **tek referans Google Earth**'tür:

1) **Davranış parity:** Navigasyon (pan, orbit, zoom, tilt), tile yükleme, terrain rendering, LOD geçişleri — hepsi Google Earth referanslıdır.
2) **Mimari parity:** Tile pyramid, SSE LOD, tile state machine, async elevation, 3-aşamalı frame pipeline, worker-based decode/mesh — Google Earth WASM RE bulgularına dayanır.
3) **UX parity:** Smooth animasyonlar, pop-free tile geçişleri, terrain-aware kamera — Google Earth deneyimi hedeflenir.

> **NOT:** `globe-web-html/libs/webglobe.js` artık parity hedefi değildir. Sadece mevcut API yüzeyinin anlaşılması için legacy kod referansı olarak kullanılabilir.

## Dokümanlar (Consolidate edilmiş — 2026-02-06)

### Planlama & Takip
- `docs/MASTER_DEVELOPMENT_PLAN.md` — **ANA GELİŞTİRME PLANI** (7 faz, 3 hafta, refactoring durumu dahil)

### Google Earth Tersine Mühendislik + Geliştirme Planları
- `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md` — **ANA TEKNİK REFERANS** (3 bölüm birleşik: GE WASM RE + 3D Terrain Planı + Tile Pipeline Optimizasyon Planı, 2026-02-06)
- `docs/GOOGLE_EARTH_MOUSE_NAVIGATION_ANALYSIS.md` — GE navigasyon RE (kamera, orbit, zoom, momentum)

### Genel
- `README.md` — Proje genel açıklaması (build/run notları)

## Kaynak Referansları

### Birincil Referans (Parity Hedefi)
- `google_earth/` — **ANA REFERANS** — WASM, WAT, reconstructed headers, string dumps
- `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md` — WASM RE bulguları (tile, DEM, render, threading, cache)
- `docs/GOOGLE_EARTH_MOUSE_NAVIGATION_ANALYSIS.md` — Navigasyon RE (kamera, orbit, zoom, momentum)

### Legacy Kod Referansı (Sadece API yüzeyi için)
- `globe-web-html/libs/webglobe.js` — Eski JS kaynak (minified, 2.2MB) — parity hedefi DEĞİL
- `webglobe_deobfuscated_v2/**` — Deobfuscate edilmiş JS kaynak
- `webglobe_api_docs/` — WebKüre API modüler dokümantasyon

## Mimari Uyum İçin Öncelikli Yapılar:
- TileKey (QuadKey, Parent/Child/Neighbor navigation)
- SSE-based LOD selection (Screen-Space Error)
- Skirt generation (LOD seam prevention)
- Tile state machine, Async elevation query
- **Frame Pipeline:** DoFrame → BuildNextScene → RenderScene (3-aşamalı)
- **DEM Pipeline:** BatchGetElevationsByPoint → RefinedElevationsRequester → TerrainMesh
- **Unpop/Crossfade:** Progressive tile loading with uUnpopBlend + RASTER_CROSSFADE
- **uCornerLods:** Bilinear LOD interpolation for smooth tile transitions
- **Mirth Engine:** geo/render/mirth/ — iç render engine kaynak yol haritası

## Mimari Değişiklik Kuralı
- `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md` mimari hedef referansıdır.
- `docs/MASTER_DEVELOPMENT_PLAN.md` yürütme planıdır.
- Yeni mimari değişiklikler bu dokümanlara dayanmalı ve plan fazlarıyla uyumlu olmalıdır.
- Parity'yi etkileyen her değişiklikte plan fazı referansı belirtilmelidir.

## Kaldırılan Dokümanlar (2026-02-06 Consolidation)
> Aşağıdakiler birleştirilip kaldırıldı. İçerikleri yukarıdaki dokümanların içine merge edildi:
> - ~~`docs/GOOGLE_EARTH_INTEGRATION_REPORT.md`~~ → DEEP_ANALYSIS'e merge
> - ~~`docs/GOOGLE_EARTH_REWRITE_BLUEPRINT.md`~~ → DEEP_ANALYSIS'e merge
> - ~~`docs/GLOBE_ENGINE_REFACTORING_PLAN.md`~~ → MASTER_DEVELOPMENT_PLAN'a merge
> - ~~`docs/NEW_ARCHITECTURE.md`~~ → MASTER_DEVELOPMENT_PLAN'a merge
> - ~~`docs/DEVELOPMENT_STATUS.md`~~ → MASTER_DEVELOPMENT_PLAN'a merge
> - ~~`docs/3D_TERRAIN_DEVELOPMENT_PLAN.md`~~ → DEEP_ANALYSIS BÖLÜM B'ye merge
> - ~~`docs/TILE_PIPELINE_OPTIMIZATION_PLAN.md`~~ → DEEP_ANALYSIS BÖLÜM C'ye merge

## C++ API Katmanı
- `src/globe_api.h` — Override edilen API listesi.
- `src/globe_api.cpp` — Gerçek implementasyonlar.
- `src/globe_api_generated.h` — Generated API deklarasyonları.
- `src/globe_api_generated.cpp` — Stub implementasyonlar (`Value::Null()`).
- `src/value.h` — `Value` tipleri ve JSON-benzeri yapı.

## Engine / Core
- `src/globe_engine.h` — Engine API, sabitler (GLOBE_RADIUS*, vb.).
- `src/globe_engine.cpp` — Kamera, animasyon, raster, query, dönüşümler.
- `src/layer_manager.h` — Layer API, feature yapılandırmaları.
- `src/layer_manager.cpp` — Query, geometry yardımcıları (PointInPolygon vb.).
