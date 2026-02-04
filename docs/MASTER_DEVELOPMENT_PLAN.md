# Native Globe - Master Geliştirme Planı

**Tarih:** 2026-02-03  
**Versiyon:** 1.0  
**Hedef:** JS API Parity + Google Earth Mimari Yakınsama

---

## Genel Bakış

Bu plan, `native_globe` projesinin geliştirilmesi için 3 haftalık (15 iş günü) bir yol haritası sunar.

### Temel Hedefler (AGENTS.md'den)
1. **API/Behavior Parity:** `webglobe.js` ile tam uyum (358 API)
2. **Mimari Yakınsama:** Google Earth benzeri core globe mimarisi

### Öncelik Kuralı
> Çatışma olursa **API parity önceliklidir**. Navigasyon için **Google Earth davranışı** esas alınır.

---

## Mevcut Durum Özeti

| Metrik | Değer |
|--------|-------|
| Toplam API | 358 |
| İmplemente Edilmiş (tahmini) | ~120 |
| Kritik Eksik | ~40 |
| Kod Satırı (globe_engine.cpp) | 11,546 |
| Tile Sistemi | Legacy + Modüler karışık |

### Tespit Edilen Kritik Sorunlar

#### 🔴 P0 - Acil (Görsel Hata / Crash Riski)
1. **LOD Rekürsiyon Guard Delik Sorunu** - `CollectVisibleTilesRecursive` limit aşıldığında fallback eklemeden dönüyor → görünür boşluklar (line 2832, 2856, 2886)
2. **Edge Stitching 2:1 LOD Sınırı** - Komşu LOD farkı sınırlanmıyor, 2+ farkta çatlak riski (line 1980, tile_mesh_builder:169)
3. **Scheduler + Worker Decode Uyumsuzluğu** - Base tile decode'da data boşaltılıyor, scheduler ham data bekliyor → kırık decode (line 7846, 7857)

#### 🟠 P1 - Yüksek (Mimari Borç)
4. **İki Paralel State Makinesi** - Base: legacy queue + TextureState / Overlay: scheduler + TileLoadState → debug/telemetri yanlış
5. ~~Dual Renderer çakışması~~ ✅ **ÇÖZÜLDÜ** (TileRenderer kaldırıldı)
6. **TileTextureManager Kullanılmıyor** - QueueUpload çağrılmıyor, ProcessUploads boş çalışıyor → gölge kod
7. **TileLodSelector Devre Dışı** - "rendering bugs" ile bypass edilmiş, SSE/GE planı ile ayrışma

#### 🟡 P2 - Orta
8. **Mesh Builder Normal Hesaplama** - Edge stitching sonrası normaller yeniden hesaplanmıyor (lighting kapalı olduğu için görünmüyor)
9. **Scheduler Queue Şişmesi** - Promote durumunda aynı key tekrar ekleniyor

---

## FAZ 1: Kritik Hata Düzeltmeleri (4 gün)

### Hedef
Görsel hatalar ve crash risklerini ortadan kaldır.

### Görevler

| ID | Görev | Öncelik | Süre | Dosya |
|----|-------|---------|------|-------|
| 1.1 | **LOD Rekürsiyon Guard Fallback** - Guard tetiklendiğinde mevcut tile'ı fallback olarak ekle | P0 | 2s | `globe_engine.cpp:2832` |
| 1.2 | **Komşu LOD Fark Sınırı** - Max 1 seviye fark enforce et veya multi-level stitch | P0 | 4s | `globe_engine.cpp:1980` |
| 1.3 | **Scheduler/Worker Decode Uyumu** - Worker decode'u scheduler path'te devre dışı bırak veya pixel veri akışı düzelt | P0 | 3s | `globe_engine.cpp:7846` |
| 1.4 | Scheduler race condition fix (`pendingKeys_` lock) | P1 | 2s | `tile_scheduler.cpp` |
| 1.5 | `pendingDecodes_` queue max size limiti | P1 | 1s | `tile_scheduler.cpp` |

### Çıktılar
- [ ] LOD delik sorunu çözüldü (görsel test)
- [ ] Edge stitching çatlakları yok
- [ ] Scheduler decode path çalışıyor

---

## FAZ 2: Mimari Birleştirme (4 gün)

### Hedef
İki paralel state makinesini tekleştir, gölge kodu temizle.

### Görevler

| ID | Görev | Öncelik | Süre | Dosya |
|----|-------|---------|------|-------|
| 2.1 | **State Machine Tekleştirme** - `TileLoadState` tek kaynak, `TextureState` kaldır | P0 | 4s | `tile.h`, `globe_engine.cpp` |
| 2.2 | **Scheduler Base Layer Entegrasyonu** - Legacy queue'yu scheduler'a taşı VEYA scheduler'ı devre dışı bırak | P0 | 4s | `globe_engine.cpp:9102` |
| 2.3 | **TileTextureManager Kaldır veya Bağla** - Ya render path'e entegre et ya da kaldır | P1 | 2s | `tile_texture_manager.*` |
| 2.4 | **TileLodSelector Reaktivasyonu** - Legacy selector yerine SSE-based selector aktif et | P1 | 3s | `tile_lod_selector.cpp` |
| 2.5 | `SyncRasterTiles` → Modüler TileSync sınıfına taşı | P2 | 3s | `globe_engine.cpp` |

### Çıktılar
- [ ] Tek state machine (`TileLoadState`)
- [ ] Tek loading pipeline (scheduler veya legacy)
- [ ] Gölge kod temizlendi

---

## FAZ 3: API Parity - Navigasyon (3 gün)

### Hedef
Kritik navigasyon API'lerini JS parity'ye getir, Google Earth davranışıyla uyumlu hale getir.

### Görevler

| ID | Görev | Öncelik | Süre |
|----|-------|---------|------|
| 3.1 | `FlyTo*` animasyon JS parity kontrolü | P0 | 3s |
| 3.2 | Mouse wheel zoom Google Earth parity | P0 | 2s |
| 3.3 | Tilt/Pan inertia fine-tuning | P1 | 2s |
| 3.4 | Keyboard navigation (arrow keys) | P1 | 1s |
| 3.5 | `SetNavigationLOD` / `SetNavigationDist` parity | P1 | 2s |

### Çıktılar
- [ ] Navigasyon parity test suite
- [ ] Google Earth benzeri zoom/pan hissiyatı
- [ ] JS API compatibility %100

---

## FAZ 4: API Parity - Layer & Query (3 gün)

### Hedef
Layer yönetimi ve query API'lerini tamamla.

### Görevler

| ID | Görev | Öncelik | Süre |
|----|-------|---------|------|
| 4.1 | `AddRaster` / `DeleteRaster` tam parity | P0 | 2s |
| 4.2 | `QueryByScreen` / `QueryByBbox` feature array dönüşü | P0 | 3s |
| 4.3 | `GeoJSONToObjectArrData` polygon/multipolygon | P1 | 2s |
| 4.4 | Layer visibility / opacity kontrolleri | P1 | 1s |
| 4.5 | Layer z-index sıralaması | P2 | 2s |

### Çıktılar
- [ ] Multi-layer rendering
- [ ] Spatial query API tam fonksiyonel
- [ ] GeoJSON import/export

---

## FAZ 5: API Parity - Draw & Style (2 gün)

### Hedef
Draw API'lerini ve stil sistemini tamamla.

### Görevler

| ID | Görev | Öncelik | Süre |
|----|-------|---------|------|
| 5.1 | `DrawIcon` / `DrawLabel` implementasyonu | P0 | 3s |
| 5.2 | `DrawCircle` / `DrawPolygon` implementasyonu | P1 | 2s |
| 5.3 | Style object parsing (color, opacity, lineWidth) | P1 | 2s |
| 5.4 | Selection highlight stili | P2 | 1s |

### Çıktılar
- [ ] 26 Draw API implement
- [ ] Styling parity with JS

---

## FAZ 6: DEM & Elevation (2 gün)

### Hedef
Yükseklik verisi ve terrain mesh sistemini stabilize et.

### Görevler

| ID | Görev | Öncelik | Süre |
|----|-------|---------|------|
| 6.1 | DEM stitching seam fix | P0 | 3s |
| 6.2 | `SampleTerrainHeightMeters` async callback | P1 | 2s |
| 6.3 | Elevation profile API | P1 | 2s |
| 6.4 | Line-of-sight analysis | P2 | 1s |

### Çıktılar
- [ ] Seamless terrain rendering
- [ ] Elevation query API

---

## FAZ 7: Polish & Release (2 gün)

### Hedef
Final polish, performans optimizasyonu, dokümantasyon.

### Görevler

| ID | Görev | Öncelik | Süre |
|----|-------|---------|------|
| 7.1 | Memory profiling & leak hunt | P0 | 2s |
| 7.2 | FPS optimization (target: 60 FPS @ LOD 18) | P1 | 2s |
| 7.3 | README güncellemesi | P1 | 1s |
| 7.4 | API dokümantasyonu güncellemesi | P2 | 2s |
| 7.5 | Release build & test | P0 | 1s |

### Çıktılar
- [ ] Release-ready binary
- [ ] API documentation
- [ ] Performance baseline

---

## Timeline Özeti

```
Hafta 1: [FAZ 1: Stabilizasyon] [FAZ 2: Tile Refactor..........]
Hafta 2: [FAZ 3: Navigasyon....] [FAZ 4: Layer & Query........]
Hafta 3: [FAZ 5: Draw] [FAZ 6: DEM....] [FAZ 7: Polish]
```

---

## Başarı Kriterleri

| Kriter | Hedef |
|--------|-------|
| API Parity | %90+ (320/358 API) |
| Crash-free Runtime | 8 saat continuous |
| FPS @ LOD 18 | 60 FPS stable |
| Memory Leak | 0 (Valgrind clean) |
| Test Coverage | %60+ |

---

## Risk Faktörleri

| Risk | Olasılık | Etki | Mitigasyon |
|------|----------|------|------------|
| JS davranış belirsizliği | Yüksek | Orta | Deobfuscated kaynak analizi |
| DEM service downtime | Orta | Yüksek | Local cache fallback |
| OpenGL compatibility | Düşük | Yüksek | Metal backend opsiyonu |

---

## Referanslar

- `AGENTS.md` - Ana kural seti
- `docs/API_PORT_REVIEW_PROMPT.md` - Detaylı API listesi
- `docs/GOOGLE_EARTH_REWRITE_BLUEPRINT.md` - Mimari hedef
- `webglobe_api_docs/` - JS API dokümantasyonu
