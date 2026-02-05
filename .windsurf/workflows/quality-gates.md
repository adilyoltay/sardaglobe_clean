---
description: SardaGlobe quality gates ve check listesi
---

# Quality Gates

Her değişiklik öncesi ve sonrası kontrol edilecek kalite kapıları.

## 1. Parity Check
- [ ] Davranış `globe-web-html/libs/webglobe.js` ile eşleşiyor
- [ ] Navigation exception kapsamındaysa Google Earth davranışı esas

## 2. LOD ve SSE Sanity
- [ ] LOD selection distance ile monotonic
- [ ] Küçük kamera değişikliklerinde stabil
- [ ] SSE thresholds, tilt factor, activation constants JS ile uyumlu

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
- [ ] Phase tamamlandıysa `docs/API_PORT_REVIEW_PROMPT.md` güncellendi
  - Phase log işaretlendi
  - Snapshot metrikleri güncellendi
