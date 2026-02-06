---
description: SardaGlobe quality gates ve check listesi
---

# Quality Gates

Her değişiklik öncesi ve sonrası kontrol edilecek kalite kapıları.

## 1. Parity Check
- [ ] Davranış Google Earth referansıyla eşleşiyor (tek parity hedefi)
- [ ] GE WASM RE dokümanları kontrol edildi (`docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md`)

## 2. LOD ve SSE Sanity
- [ ] LOD selection distance ile monotonic
- [ ] Küçük kamera değişikliklerinde stabil
- [ ] SSE thresholds, tilt factor, activation constants Google Earth RE ile uyumlu

## 3. Tile State Machine
- [ ] State transitions valid
- [ ] Stuck state yok
- [ ] Redundant load yok

## 4. Async Elevation
- [ ] DEM requests cached
- [ ] Render bloklanmıyor

## 5. Rendering Stability
- [ ] Visible seam yok
- [ ] Popping minimized (skirts veya morphing ile)

## 6. Tests
```bash
# Numeric parity test
./build/tests/lod_conformance_test

# Visual regression test
./build/tests/visual_lod_test
```

## 7. Documentation
- [ ] Değişiklik ilgili teknik dokümanda kaydedildi
