# AGENTS.md — Native Globe Reference Index

Bu dosya, projedeki ana dokümanları ve kaynak referanslarını tek noktadan listeler.
Her faz tamamlandığında `docs/API_PORT_REVIEW_PROMPT.md` içindeki **Faz Tamamlama Günlüğü**
ve **Güncel Durum Snapshot** bölümleri güncellenmelidir.

## Ana Master Kural
**Temel hedef (ikili):**
1) **API/Behavior parity:** `globe-web-html/libs/webglobe.js` davranışları ve API yüzeyiyle **tam parity** sağlamak.  
2) **Core mimari hedef:** Google Earth benzeri bir çekirdek globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation, vb.) **yakınsamak**.

> Çatışma olursa **API/behavior parity önceliklidir**, mimari dönüşüm parity’yi bozmayacak şekilde yapılır.

> **NOT:** 2026-01-29 itibariyle ana referans `webglobe/main.js` yerine `globe-web-html/libs/webglobe.js` olarak değiştirilmiştir.

## Navigasyon Parity Kuralı (İstisna)
Navigasyon davranışlarında (mouse/keyboard pan-orbit-zoom-tilt) **JS yerine Google Earth parity** esas alınacaktır.  
JS ile çelişki varsa, **navigasyon için Google Earth davranışı önceliklidir**.

## Dokümanlar (Consolidate edilmiş — 2026-02-06)

### Planlama & Takip
- `docs/MASTER_DEVELOPMENT_PLAN.md` — **ANA GELİŞTİRME PLANI** (7 faz, 3 hafta, refactoring durumu dahil)
- `docs/API_PORT_REVIEW_PROMPT.md` — API parity checklist + faz takibi + durum snapshot'ı

### Google Earth Tersine Mühendislik + Geliştirme Planları
- `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md` — **ANA TEKNİK REFERANS** (3 bölüm birleşik: GE WASM RE + 3D Terrain Planı + Tile Pipeline Optimizasyon Planı, 2026-02-06)
- `docs/GOOGLE_EARTH_MOUSE_NAVIGATION_ANALYSIS.md` — GE navigasyon RE (kamera, orbit, zoom, momentum)

### Genel
- `README.md` — Proje genel açıklaması (build/run notları)
- `webglobe_api_docs/` — WebKüre API modüler dokümantasyon

## Kaynak Referansları (Güncel)
- `globe-web-html/libs/webglobe.js` — **ANA JS KAYNAK** (minified, 2.2MB), davranış parity referansı.
- `webglobe_deobfuscated_v2/**` — **Güncel** deobfuscate edilmiş JS kaynak (webglobe.js'den).
- `webglobe_deobfuscated_v2/webglobe_beautified.js` — Beautified tam kaynak (67,818 satır).

## Google Earth Tersine Mühendislik Referansları (Mimari Hedef)
- `google_earth/` — Kaynak dizin (WASM, WAT, reconstructed headers)

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

## Faz Güncelleme Kuralı
Her faz tamamlandığında aşağıdaki güncellemeler yapılır:
1) `docs/API_PORT_REVIEW_PROMPT.md` → **Faz Tamamlama Günlüğü** işaretlenir.
2) `docs/API_PORT_REVIEW_PROMPT.md` → **Güncel Durum Snapshot** metrikleri güncellenir.
3) Gerekirse **Mevcut İmplementasyonlar** listesi revize edilir.

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
