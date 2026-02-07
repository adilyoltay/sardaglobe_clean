# AGENTS.md — Native Globe Reference Index

Bu dosya, projedeki ana dokümanları ve kaynak referanslarını tek noktadan listeler.

## Ana Master Kural
**Tek parity hedefi: Google Earth**

Amacımız Google Earth kalitesinde bir globe engine geliştirmektir. Tüm davranış, mimari ve UX kararlarında **tek referans Google Earth**'tür:

1) **Davranış parity:** Navigasyon (pan, orbit, zoom, tilt), tile yükleme, terrain rendering, LOD geçişleri — hepsi Google Earth referanslıdır.
2) **Mimari parity:** Tile pyramid, SSE LOD, tile state machine, async elevation, 3-aşamalı frame pipeline, worker-based decode/mesh — Google Earth WASM RE bulgularına dayanır.
3) **UX parity:** Smooth animasyonlar, pop-free tile geçişleri, terrain-aware kamera — Google Earth deneyimi hedeflenir.

> **NOT:** `globe-web-html/libs/webglobe.js` artık parity hedefi değildir. Sadece mevcut API yüzeyinin anlaşılması için legacy kod referansı olarak kullanılabilir.

## Dokümanlar

- `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md` — **ANA TEKNİK REFERANS** (3 bölüm birleşik: GE WASM RE + 3D Terrain Planı + Tile Pipeline Optimizasyon Planı)
- `docs/GOOGLE_EARTH_PRO_DESKTOP_RE_ANALYSIS.md` — **GE Pro Desktop Native Binary RE** (earth::evll sınıf hiyerarşisi, IG render engine, Drawable sistem, Navigation detay, Proto şemaları)
- `docs/GOOGLE_EARTH_MOUSE_NAVIGATION_ANALYSIS.md` — GE navigasyon RE (kamera, orbit, zoom, momentum)
- `docs/MASTER_DEVELOPMENT_PLAN.md` — 7-faz geliştirme yol haritası
- `README.md` — Proje genel açıklaması (build/run notları)

## Kaynak Referansları

### Birincil Referans (Parity Hedefi)
- `google_earth/` — **ANA REFERANS** — WASM, WAT, reconstructed headers, string dumps

### Legacy Kod Referansı (Sadece API yüzeyi için)
- `globe-web-html/libs/webglobe.js` — Eski JS kaynak (minified, 2.2MB) — parity hedefi DEĞİL
- `webglobe_deobfuscated_v2/**` — Deobfuscate edilmiş JS kaynak

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

## Dosya Haritası

Detaylı modül ve dosya haritası için: `/engine-map` workflow.
