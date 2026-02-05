# WebGlobe API C++ Port - Kapsamlı Review Promptu

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Proje Bağlamı

JavaScript tabanlı WebGlobe API'sinin C++ native uygulamasına port edilmesi projesi.

| Dosya | Açıklama |
|-------|----------|
| `/globe-web-html/libs/webglobe.js` | **ANA KAYNAK** (minified, ~68K satır, 2.2MB) |
| `/webglobe_deobfuscated_v2/` | Deobfuscate edilmiş JS kaynak |
| `/src/globe_api.h` | C++ API header |
| `/src/globe_api.cpp` | C++ gerçek implementasyonlar |
| `/src/globe_api_generated.h` | Generated API declarations |
| `/src/globe_api_generated.cpp` | Stub implementasyonlar |
| `/webglobe_deobfuscated_v2/api_list.txt` | API listesi (358 fonksiyon) |
| `/webglobe_api_docs/` | WebKüre API dokümantasyonu (modüler) |

> **NOT:** 2026-01-29 itibariyle ana referans `webglobe/main.js` yerine `globe-web-html/libs/webglobe.js` olarak değiştirilmiştir.

---

## 1. API Kapsam Analizi

Aşağıdaki dosyaları incele ve implementasyon durumunu belirle:

### İncelenecek Dosyalar
- `/src/globe_api.h` - Override edilen API'lerin listesi
- `/src/globe_api.cpp` - Gerçek implementasyonlar
- `/src/globe_api_generated.cpp` - Stub implementasyonlar (`Value::Null()` dönenler)
- `/webglobe_api_docs/` - JS API dokümantasyonu (modüler referans)

### Sorular
1. Toplam 365 API'den kaç tanesi gerçekten implement edilmiş (`Value::Null()` dönmeyen)?
2. Kritik API kategorilerinin implementasyon durumunu listele:

| Kategori | API Örnekleri | Implement Edilen | Toplam |
|----------|---------------|------------------|--------|
| Navigation/Camera | `FlyTo*`, `SetCamera*`, `Get*Position` | ? | ? |
| Layer Management | `AddLayer`, `DeleteLayer`, `GetLayer*` | ? | ? |
| Query | `QueryBy*`, `GetGeoFromScreenPoint` | ? | ? |
| Rendering | `Draw3d*`, `DrawCircle`, `DrawIcon` | ? | ? |
| Coordinate Conversion | `GeoTo*`, `*ToGeo` | ? | ? |
| Raster/Tile | `AddRaster`, `SetRaster*` | ? | ? |

---

## 2. Parametre Sayısı Uyumu

`/api_list.json` dosyasındaki parametre sayıları ile C++ implementasyonlarını karşılaştır.

### Örnek Kontrol
```json
// api_list.json
"api_FlyToPoint": 5
```

```cpp
// globe_api.h
Value api_FlyToPoint(const Value& a0, const Value& a1, const Value& a2, const Value& a3, const Value& a4);
// ✓ 5 parametre - DOĞRU
```

### Kontrol Listesi
- [ ] Her API'nin parametre sayısı JSON ile eşleşiyor mu?
- [ ] Parametreler doğru sırada mı kullanılıyor?
- [ ] Optional parametreler doğru handle ediliyor mu?

---

## 3. Return Value Analizi

JavaScript API'lerinin dönüş değerlerini C++ karşılıkları ile karşılaştır.

### Kritik Dönüş Tipleri

| Tip | Beklenen C++ Karşılığı | Kontrol |
|-----|------------------------|---------|
| Koordinat | `{long, lat}` veya `{x, y}` object | [ ] |
| Array | `Value::Array()` | [ ] |
| Boolean | `Value::Bool()` | [ ] |
| Number | `Value::Number()` | [ ] |
| Object | `Value::Object()` | [ ] |

### Bilinen Sorunlu Pattern (Düzeltildi)

```cpp
// globe_api.cpp
// ✅ Artık object dönüyor: {lat, lon, long, lng}
Value GlobeApi::api_GetScreenCenterAsDegree() { ... }
```

**Düzeltme Gereken API'ler:**
- [x] `api_GetScreenCenterAsDegree` → `{long, lat}` object dönmeli
- [x] `api_GetCurrentLookInfo` → Full camera state object dönmeli (JS-style key’ler eklendi)
- [x] `api_GetGeoFromScreenPoint` → `{long, lat}` object dönmeli
- [x] `api_GetScreenPointFromGeo` → `{x, y}` object dönmeli
- [x] `api_QueryByScreen` → Feature array dönmeli

---

## 4. İmplementasyon Doğruluğu

Aşağıdaki implement edilmiş API'leri JavaScript karşılıkları ile karşılaştır:

### Detaylı Analiz Tablosu

| C++ API | Beklenen Davranış | Kontrol Noktaları | Durum |
|---------|-------------------|-------------------|-------|
| `api_FlyToPoint` | Animasyonlu geçiş | lon/lat sırası, altitude→distance dönüşümü | [ ] |
| `api_FlyToPointDirect` | Anında geçiş | Aynı kontroller | [ ] |
| `api_FlyToRegion` | Bölgeye zoom | BBOX hesaplaması | [ ] |
| `api_SetCameraPos` | 6 param (lon,lat,alt,pitch,yaw,roll) | Roll kullanılıyor mu? | [ ] |
| `api_GetCurrentLOD` | Zoom seviyesi | Integer mi, decimal mi? | [ ] |
| `api_GetCurrentLODWithDecimal` | Zoom (ondalıklı) | Decimal değer dönüyor mu? | [ ] |
| `api_QueryByScreen` | Feature listesi | Array mı, count mu? | [ ] |
| `api_SetNavigationLOD` | Zoom ayarla | Min/max sınırları | [ ] |
| `api_SetTiltAngle` | Pitch ayarla | Açı limitleri | [ ] |
| `api_SetNorthAngle` | Yaw ayarla | 0-360 normalizasyon | [ ] |

---

## 5. Eksik Kritik API'ler

### P0 - Zorunlu (Uygulama çalışması için gerekli)

- [x] `api_GlobeVersion` - Versiyon bilgisi
- [x] `api_GetCurrentGeometry` - Sphere/Flat mode
- [x] `api_SetGeometry` - Geometry mode değiştirme
- [x] `api_GetMouseDeg` - Mouse pozisyonu (geo)
- [x] `api_GetMousePos` - Mouse pozisyonu (screen)
- [x] `api_GetGL` - WebGL context (C++'da OpenGL context)

### P1 - Yüksek Öncelik (Temel fonksiyonalite)

- [x] `api_AddRaster` - Tile layer ekleme
- [x] `api_SetRasterService` - Tile URL yapılandırma
- [x] `api_DeleteRaster` - Tile layer silme
- [x] `api_GeoJSONToObjectArrData` - GeoJSON parse
- [x] `api_ObjectCreator` - Obje oluşturma
- [x] `api_GetDefaultStyle` - Varsayılan stil
- [x] `api_GetDefaultLayerStyle` - Varsayılan layer stili
- [x] `api_SetMouseEvents` - Mouse event handler

### P2 - Orta Öncelik (Gelişmiş özellikler)

**Draw API'leri (26 adet):**
- [x] `api_Draw3dLine`, `api_Draw3dLineLoop`, `api_Draw3dLineStrip`
- [x] `api_Draw3dPolygon`, `api_Draw3dPolygonStrip`
- [x] `api_Draw3dDashedLine`, `api_Draw3dDashedLineLoop`, `api_Draw3dDashedLineStrip`
- [x] `api_DrawCircle`, `api_DrawIcon`, `api_DrawContextText`

**Get3D API'leri (8 adet):**
- [x] `api_Get3DPoint`, `api_Get3DPoints`
- [x] `api_Get3DPointsByGeoArr`, `api_Get3DPointsByGeoArr_SameZ`
- [x] `api_GetCartesian3DPoint`, `api_GetCartesian3DPoints`

**Coordinate Conversion (12 adet):**
- [x] `api_GeoToDMS`, `api_DMSToGeo`
- [x] `api_GeoToUTM`, `api_UTMToGeo`
- [x] `api_GeoToMGRS`, `api_MGRSToGeo`
- [x] `api_GeoToGeoRef`, `api_GeoRefToGeo`
- [x] `api_GetMercatorPoint`, `api_GetMercator2DPoint`, `api_GetMercator3DPoint`

### P3 - Düşük Öncelik (Opsiyonel özellikler)

- [ ] Edit API'leri (`StartEditObj`, `StopEditObj`, `Undo*`, `Redo*`)
- [ ] Plugin API'leri (`RegisterPlugin`, `GetPlugin`, vb.)
- [ ] Visibility Analysis API'leri
- [ ] 3D Model API'leri

---

## 6. Matematik ve Algoritma Doğruluğu

JavaScript'teki matematiksel fonksiyonların C++ karşılıklarını doğrula.

### Kritik Fonksiyonlar

```javascript
// JavaScript (webglobe.js)
A.b.Dist2DV(x1, y1, x2, y2)                    // 2D mesafe
A.b.GeodesicDistance(lon1, lat1, lon2, lat2)   // Geodesic mesafe
A.b.AzimuthOf2PointsOnSphere(p1, p2)           // Azimut açısı
A.b.DegreeToRadyan(deg)                        // Derece → Radyan
A.b.RadyanToDegree(rad)                        // Radyan → Derece
A.b.GeoTo3DP(geo, radius, out)                 // Geo → 3D Cartesian
A.b.CSPointInPolygon(lat, lon, polygon)        // Point-in-polygon test
A.b.CSNormalizeAngle(angle)                    // Açı normalizasyonu
```

### Doğrulama Test Değerleri

| Fonksiyon | Girdi | Beklenen Çıktı |
|-----------|-------|----------------|
| `Dist2DV` | (0,0,3,4) | 5.0 |
| `GeodesicDistance` | (0,0,0,1) | ~111.32 km |
| `DegreeToRadyan` | 180 | π (3.14159...) |
| `AzimuthOf2PointsOnSphere` | İstanbul→Ankara | ~90° (yaklaşık doğu) |

---

## 7. Constant/Enum Uyumu

JavaScript sabitlerinin C++ karşılıklarını kontrol et.

### Globe Constants

```javascript
// JavaScript
uo.GLOBE_RADIUS_K          // Globe radius coefficient
uo.GLOBE_RADIUS            // Earth radius (meters)
uo.GLOBE_Z_ABART           // Z-axis scaling
uo.SHAPE_MAX_STEP_ANGLE    // Max angle for shape tessellation
uo.SHAPE_MIN_STEP_ANGLE    // Min angle for shape tessellation
uo.SHAPE_STEP_ANGLE        // Default step angle
```

### Geometry Calculation Types

```javascript
// JavaScript
Ga.GEODESIC      // Geodesic distance calculation
Ga.GREAT_CIRCLE  // Great circle distance calculation
```

### Geometry Modes

```javascript
// JavaScript
ba.SPHERE  // 3D sphere mode
ba.FLAT    // 2D flat/mercator mode
```

---

## 8. Callback/Event Sistemi

JavaScript callback mekanizmalarının C++ karşılıkları:

| JavaScript Callback | Açıklama | C++ Karşılığı | Durum |
|---------------------|----------|---------------|-------|
| `api_SetCameraCallBack` | Kamera değişiklik callback'i | std::function? | [ ] |
| `api_SetStatusBarCallBack` | Status bar callback'i | | [ ] |
| `api_SetMouseEvents` | Mouse event handler'ları | | [ ] |
| `api_SetUndoBuffersChangedEvent` | Undo event'i | | [ ] |
| `api_EditCallbackChanged` | Edit callback | | [ ] |
| `api_EditCallbackCreator` | Creator callback | | [ ] |

---

## 9. Test Senaryoları

Her implement edilmiş API için test senaryoları:

### Navigation Tests

```cpp
// Test 1: FlyToPoint
api_FlyToPoint(29.0, 41.0, 10000, 2.0, null)
// Beklenen: İstanbul'a 2 saniyede uçuş, altitude 10km

// Test 2: SetCameraPos
api_SetCameraPos(29.0, 41.0, 5000, 45, 0, 0)
// Beklenen: İstanbul, 5km yükseklik, 45° tilt, kuzey yönü

// Test 3: FlyToRegion
api_FlyToRegion(26.0, 36.0, 45.0, 42.0, 1.0)
// Beklenen: Türkiye'yi kapsayan görünüm
```

### Query Tests

```cpp
// Test 4: GetGeoFromScreenPoint
api_GetGeoFromScreenPoint(width/2, height/2)
// Beklenen: Ekran merkezinin geo koordinatları {long, lat}

// Test 5: GetScreenPointFromGeo
api_GetScreenPointFromGeo(29.0, 41.0)
// Beklenen: İstanbul'un ekran koordinatları {x, y}
```

### Layer Tests

```cpp
// Test 6: Layer Management
api_AddLayer("test_layer", {...style})
result = api_LayerCount()
// Beklenen: result == 1

api_SetLayerOn("test_layer", false)
visible = api_GetLayerOn("test_layer")
// Beklenen: visible == false

api_DeleteLayer("test_layer")
result = api_LayerCount()
// Beklenen: result == 0
```

### Zoom Tests

```cpp
// Test 7: Zoom Level
api_ZoomToLOD(10)
lod = api_GetCurrentLOD()
// Beklenen: lod == 10

api_SetMinNavigationLOD(5)
api_SetMaxNavigationLOD(18)
api_ZoomToLOD(3)
lod = api_GetCurrentLOD()
// Beklenen: lod == 5 (min sınırda)
```

---

## 10. Çıktı Formatı

Analiz sonucunu aşağıdaki formatta raporla:

### Özet İstatistikler

```
┌─────────────────────────────────────┐
│ API Kapsam Raporu                   │
├─────────────────────────────────────┤
│ Toplam API sayısı:     365          │
│ Implement edilen:      XX           │
│ Stub (Value::Null):    YY           │
│ Kapsam oranı:          XX/365 = %ZZ │
└─────────────────────────────────────┘
```

### Kritik Bulgular

```
[HATA]  API_ADI - Detaylı açıklama
[EKSİK] API_ADI - Neden kritik olduğu
[UYARI] API_ADI - Potansiyel sorun açıklaması
```

### Kategori Bazlı Durum

| Kategori | Implement | Stub | Oran |
|----------|-----------|------|------|
| Navigation | X | Y | %Z |
| Layer | X | Y | %Z |
| Query | X | Y | %Z |
| Rendering | X | Y | %Z |
| Conversion | X | Y | %Z |
| Raster | X | Y | %Z |
| **TOPLAM** | **X** | **Y** | **%Z** |

### Öncelikli Aksiyon Listesi

1. [ ] En yüksek öncelikli eksik/hatalı item
2. [ ] İkinci öncelikli item
3. [ ] ...

### Parity Skoru

```
╔═══════════════════════════════════════╗
║  PARITY SKORU: XX / 100               ║
╠═══════════════════════════════════════╣
║  - API Kapsam:        XX/40 puan      ║
║  - Doğruluk:          XX/30 puan      ║
║  - Return Types:      XX/15 puan      ║
║  - Matematik:         XX/15 puan      ║
╚═══════════════════════════════════════╝
```

---

## 11. Geliştirme Planı (Fazlar)

### Faz 0 — Stabilizasyon & P0 Parity (En Kritik)
**Amaç:** Uygulama çalışabilirliği + kritik API davranış uyumu.
- P0 API’leri gerçek davranışa bağla: `api_GetMousePos`, `api_GetMouseDeg`, `api_SetGeometry` (engine/renderer ile senkron).
- Navigation doğruluğu: `api_GetCurrentLODWithDecimal`, `api_SetNavigationLOD` clamp, `api_SetNorthAngle` normalizasyon.
- Return type/parite: koordinat objeleri için `{lon,lat}` anahtar standardı; `api_QueryByScreen` output formatı netle.
- Kamera parametreleri: `api_SetCameraPos` roll ve northAngle handling; `api_FlyToPointDirect` northAngle/callback.
- Temel testler: Navigation + Query + Layer sanity senaryoları.

**Çıkış kriteri:** P0 API’lerde davranış/return uyumu + temel testler yeşil.

### Faz 1 — Raster/Layer & Core Data (Yüksek Öncelik)
**Amaç:** Harita katmanları ve veri akışı kullanılabilir olsun.
- Raster CRUD: `api_DeleteRaster`, görünürlük/opaklık/z-index ayarları.
- Layer style defaults: `api_GetDefaultStyle`, `api_GetDefaultLayerStyle`.
- GeoJSON pipeline: `api_GeoJSONToObjectArrData`, `api_ObjectCreator`.
- Query genişletme: `api_QueryByBBox`, `api_QueryByScreen` full feature çıktısı.

**Çıkış kriteri:** Raster/layer yönetimi tam, GeoJSON→objeler akışı çalışır.

### Faz 2 — Rendering & Coordinate Conversion (Orta Öncelik)
**Amaç:** Draw API’leri ve dönüşümler parity’ye yaklaşsın.
- Draw3d*/DrawCircle/DrawIcon/DrawContextText implementasyonları.
- 3D point API’leri: `api_Get3DPoint*`, `api_GetCartesian3DPoint*`.
- Coordinate conversions: `api_GeoTo*`, `api_*ToGeo`, `api_GetMercator*`.
- Event dispatch/trigger: `api_DispatchEvent`, `api_TriggerCallback` + callback registry.
- Matematik util parity: Dist2DV, Geodesic, Azimuth, NormalizeAngle vb.

**Çıkış kriteri:** Draw + conversion API’lerinin testlerle doğrulanması.

### Faz 3 — Gelişmiş Özellikler (Düşük Öncelik)
**Amaç:** Edit/Plugin/Visibility/3D model parity ve performans.
- Edit API’leri, plugin sistemi, visibility analysis.
- 3D model API’leri + performans/profiling.

**Çıkış kriteri:** Ürün gereksinimine göre seçilmiş advanced API’ler tamam.

### Faz 4A — Kamera Animasyon Parity (Orta Öncelik)
**Amaç:** Kamera animasyonları parity’ye yaklaşsın.
- FCamMode state machine implementasyonu.
- SetFlyToPoint ve continuous wheel zoom implementasyonu.
- Mid-turn inertia ile decay implementasyonu.

**Çıkış kriteri:** Kamera animasyonları parity’ye yaklaşsın.

### Faz Tamamlama Günlüğü
Her faz bittiğinde aşağıyı güncelle:
- [x] Faz 0 tamamlandı — Tarih: 2026-01-26 — Notlar: LOD decimal, mouse cursor, north angle normalize, QueryByScreen object output.
- [x] Faz 1 tamamlandı — Tarih: 2026-01-26 — Notlar: Raster CRUD+zIndex, GeoJSON string parse, ObjectCreator, QueryByBBox, MouseEvents.
- [x] Faz 2 tamamlandı — Tarih: 2026-01-26 — Notlar: Conversion API'leri, 3D point API'leri, Draw* command recording, callback/event registry.
- [x] Faz 4A tamamlandı — Tarih: 2026-01-28 — Notlar: Kamera animasyon parity (FCamMode state machine, SetFlyToPoint, continuous wheel zoom, mid-turn inertia with decay).
- [x] Faz 4B tamamlandı — Tarih: 2026-01-28 — Notlar: 2D/Flat mode navigation (FlatNavigation class, Mercator projection, 2D pan/zoom).
- [x] Faz 4C tamamlandı — Tarih: 2026-01-28 — Notlar: LOD distance table (ao array), screen position history (undo/redo).
- [x] Faz 3 tamamlandı — Tarih: 2026-01-28 — Notlar: Edit API'leri (StartEditObj, StopEditObj, Undo, Redo), Plugin API'leri (RegisterPlugin, GetPlugin, GetAllPluginsId), Navigation history (GoToPreviousPosition, TurnToNorth).
- [x] Faz 4D tamamlandı — Tarih: 2026-01-29 — Notlar: Camera/Input parity (arcball-based inertia, onGlobe gate, segment tracking, StopAnimation on mousedown, HiDPI double-click coords, RotateByAngle method).
- [x] Faz 4E tamamlandı — Tarih: 2026-01-29 — Notlar: ParitySnapshot düzeltmeleri (LOD clamp), SA-table altitude parity, DEM CN batch queue, cell division limit.
- [x] Faz 4F tamamlandı — Tarih: 2026-01-29 — Notlar: Navigation history API'leri engine history ile bağlandı, DirectPosNatural dist normalized parity, geometry switch history reset, 2D clamp flat nav limits.
- [x] Faz 10 (Navigation Parity) tamamlandı — Tarih: 2026-02-01 — Notlar: Unified FlightController mimarisi, Left-drag "Grab Earth" (anchor-based), Shift+Left Orbit/Tilt, Right-drag Dolly Zoom, Wheel Zoom, Inertia physics. **Fixed: Navigation limits unit mismatch (meters/km), 3D Keyboard Navigation, Mouse Wheel Settings propagation.**
- [x] Faz 11 (Tile Pipeline P1/P2) tamamlandı — Tarih: 2026-02-01 — Notlar: Priority Queue (Leaf-First), Scheduler Backoff, Stale Callback, No-Data Handling (AnalyzeAlpha).
- [x] Faz 12 (Tile Pipeline P3) tamamlandı — Tarih: 2026-02-01 — Notlar: Render Fallback (Target Not Ready -> Force Descendant Search), Parent Retention Logic Verified.
- [x] Faz 13 (Tile Pipeline P4/P5) tamamlandı — Tarih: 2026-02-01 — Notlar: Cache Usage Stats (lastFrameUsed update), Localized DEM Invalidation (updatedDemKeys queue).
- [x] Faz 14 (Tile Pipeline P7+) tamamlandı — Tarih: 2026-02-02 — Notlar: True Unified Scheduler (SchedulerKey). Overlay support restored. Vector backoff fixed. **Optimization:** Base Raster Pass 3 moved after Overlays, Child-First ordering implemented, LOAD_OK_NO_DATA unblocks refinement. All tests passed.
- [x] Faz 15 (Stability & Fixes) tamamlandı — Tarih: 2026-02-01 — Notlar: Fixed black screen root cause (Camera init). Re-enabled culling. Verified all P7+ fixes. System stable.
- [x] Faz 16 (Navigation & Concurrency Fixes) tamamlandı — Tarih: 2026-02-01 — Notlar: Fixed 'stuck' pan issue via Off-Globe Drag Fallback in FlightController. Fixed High-severity race conditions in pending sets using pendingMutex in GlobeEngine. Verified clean build.
- [x] Faz 16.5 (Input Synchronization) tamamlandı — Tarih: 2026-02-01 — Notlar: Fixed 'flick' on click by synchronizing OnMouseButton coordinates with OnCursorPos stream (cursorX/Y reuse) to prevent delta jumps.
- [x] Faz 16.6 (3D Input Alignment) tamamlandı — Tarih: 2026-02-01 — Notlar: Synced m_newCamera projection with Render settings (FOV/Near/Far) to fix picking accuracy. Normalized FlightController tilt/heading computation.
- [x] Faz 17 (API Expansion & Navigation Refinement) tamamlandı — Tarih: 2026-02-01 — Notlar: Implemented 17 new APIs (WMS Overlays, ObjectBuffer, Language, FlashPeriod, etc.). Fixed 3D snap-back bug in SetNorthAngle. Resolved SetDirectPos vs. Animation conflict. Optimized off-globe drag FOV sensitivity.
- [x] Faz 18 (Vector Layer & Icon Rendering Parity) tamamlandı — Tarih: 2026-02-01 — Notlar: Async IconMap load with callback (internal), Point Sprite rendering (vec4 UVs, gl_PointCoord), LayerStyle parity (icons, labels, flash, opt-in behavior), api_SetLayerStyle dynamic updates. Label Rendering integrated (LabelManager -> LayerManager), dynamic text generation from properties.
- [x] Faz 18.1 (Orbit & Zoom Refinement) tamamlandı — Tarih: 2026-02-01 — Notlar: Fixed Orbit momentum loss, relaxed tilt limits (5-175°), implemented EMA momentum, stabilized Sky-click orbit. Implemented Zoom-to-Cursor stabilization (bidirectional), dist-scaled zoom speed, and micro-pan correction for drift-free zoom. Added accumulating deadzone (0.5px).
- [x] Faz 19 (Advanced Features) tamamlandı — Tarih: 2026-02-01 — Notlar: Implemented Image Overlays (Add, Delete, Change, Color, Rotate) with proper resource cleanup. Implemented Analysis Tools (LineOfSight, Profile) using DEM sampling. Implemented Object Buffer Management (DeleteAll, Find, Count).
- [x] Faz 19.5 (LOD/Tile/Mesh Parity Refinement) tamamlandı — Tarih: 2026-02-01 — Notlar: Fixed SSE tiltFactor application, synchronized Mesh grid defaults (segments=4), and corrected DEM child order stitching for seam-free terrain. Verified build and ran `build/verify_navigation` (Passed).
- [x] Faz 19.6 (WGS84 DEM LOD Hotfix) tamamlandı — Tarih: 2026-02-01 — Notlar: Unlocked meshLevel for WGS84, implemented LOD-based tile scaling, and added safety clamps/CLI config.
- [x] Faz 20 (Navigation Parity P1) tamamlandı — Tarih: 2026-02-01 — Notlar: Implemented Quadratic/Cubic easing functions and 5-frame weighted average input smoothing. Verified build and ran `build/verify_navigation` (Passed).
- [x] Faz 21 (Navigation Parity P2) tamamlandı — Tarih: 2026-02-01 — Notlar: Implemented Bezier-based momentum decay for Pan/Orbit and Logarithmic interpolation for Zoom. Updated FlightController architecture to support deterministic time for testing. Verified build and tests. Fixed unit scaling mismatch (Meters vs KM) in Log Zoom.
- [x] Faz 22 (Navigation Parity P3) tamamlandı — Tarih: 2026-02-01 — Notlar: Implemented Adaptive Dead Zone scaling with altitude and normalized velocity. Camera is now more responsive at speed/zoom while stable at rest. Verified build and tests.
- [x] Faz 0 (DEM Parity Diagnosis) tamamlandı — Tarih: 2026-02-02 — Notlar: Added scoped Referer/Origin injection for DEM requests, implemented debug logging gated by `demDebug` flag, and verified URL generation logic.
- [x] Faz 1 (WGS84 BBox Alignment) tamamlandı — Tarih: 2026-02-02 — Notlar: Updated SampleTerrainHeightMeters to use GeoToTileXY and TileToBBox4326 for WGS84 requests, aligning coordinate bounds with JS parity. Removed static span dependency for tile indexing.
- [x] Faz 2 (Parse Robustness) tamamlandı — Tarih: 2026-02-02 — Notlar: Implemented robust DEM parsing by prioritizing "data" key and strictly requiring "[[" to confirm 2D grid structure. This prevents parsing HTML errors or flat metadata arrays as terrain. Enhanced warning logs for incomplete data.
- [x] Faz 3 (Mesh Integration) tamamlandı — Tarih: 2026-02-02 — Notlar: Validated mesh rebuild pipeline (DemWorker -> updatedDemKeys -> InvalidateTileAndNeighbors -> BuildTileMesh). Unified neighbor invalidation logic for WGS84 using Mercator indexing to match Phase 1 parity.
- [x] Faz 4 (Terrain Mesh + Tile Stitch) tamamlandı — Tarih: 2026-02-02 — Notlar: Implemented P0.1 (EdgeFlags/Snap), P0.2 (Re-stitch on Arrival), P0.3 (Mesh Integration). Removed Skirts in favor of seamless stitching. Verified via debug logs.
- [x] Faz 5 (DEM Finalization) tamamlandı — Tarih: 2026-02-02 — Notlar: Finalized documentation, confirmed stable performance settings, and ensured critical parse errors are always logged. DEM parity system is now complete and robust.
- [x] Faz 1.2 (Komşu LOD Fark Sınırı) tamamlandı — Tarih: 2026-02-04 — Notlar: LodSelector'a neighbor LOD conformance eklendi (maxNeighborDelta=1). Deterministik pass-based refinement. leafSet O(1) lookup. OpenGlobus'tan esinlenildi.
- [x] Faz 6.1 (DEM Stitching Seam Fix) tamamlandı — Tarih: 2026-02-04 — Notlar: Tile.edgeCoarserMask eklendi (N/E/S/W bit flags). BuildTileMesh'te border vertex'ler için coarser DEM level sampling. prevEdgeCoarserMask ile rebuild detection.
- [x] Faz 4 (Layer & Query MVP) tamamlandı — Tarih: 2026-02-04 — Notlar: Layer/Feature/LayerStyle structs (layer.h), LayerManager CRUD/query (layer_manager.h/cpp), ScreenToGeo/GeoToScreen (earth_camera.cpp), GlobeEngine entegrasyonu. Unit fix: km tutarlılığı.
- [x] OpenGlobus Core Port tamamlandı — Tarih: 2026-02-04 — Notlar: LonLat (lonlat.h), Ellipsoid (ellipsoid.h/cpp), Extent (extent.h), QuadTreeNode (quadtree_node.h/cpp). Tile yapısına Extent entegrasyonu. BuildTileMesh artık Ellipsoid.GeodeticToCartesian kullanıyor (WGS84 uyumlu mesh).
- [x] Tile Pipeline Opt P0-P3 tamamlandı — Tarih: 2026-02-05 — Notlar: Telemetri (p95/p99 + alt süreler), URL template (regex-free), cache I/O worker'a taşındı, decoder priority+fairness, backpressure (bounded queue + in-flight limit).
- [x] Tile Pipeline Opt P4-P6 tamamlandı — Tarih: 2026-02-05 — Notlar: Upload priority+reuse (glTexSubImage2D), async mesh scheduler + shared EBO, pin epoch + eviction budget.
- [x] 3D Terrain FAZ 1-3 tamamlandı — Tarih: 2026-02-05 — Notlar: demMeshN=65, demHeightScale=2.5, HeightmapManager (R16F GPU texture), vertex shader displacement, DEM timeout 30s, tile eviction heightmap release.
- [x] 3D Terrain Parity FAZ 0 tamamlandı — Tarih: 2026-02-05 — Notlar: DemHealthStatus enum, CheckHealth() startup probe, DemStats telemetri (fetch/parse/timeout/auth counters), debug panel DEM bölümü, config konsolidasyonu (timeout/retry/backoff).
- [x] 3D Terrain Parity FAZ 1 tamamlandı — Tarih: 2026-02-05 — Notlar: DisplacementMode enum (CPU_MESH_BAKE/GPU_HEIGHTMAP_DISPLACE), tek authority gate (mesh builder + heightmap upload + render path), --gpu-terrain CLI flag, debug panel terrain mode toggle.
- [x] 3D Terrain Parity FAZ 2 tamamlandı — Tarih: 2026-02-05 — Notlar: Terrain-aware PickGlobe (iterative DEM sphere refinement, 2-pass convergence). FlightController orbit/pan/zoom pivot artık terrain yüksekliğini dikkate alıyor.
- [x] 3D Terrain Parity FAZ 3 tamamlandı — Tarih: 2026-02-05 — Notlar: DEM request'leri TilePyramid::GetRankedRequired() ile SSE score sıralı. Yakındaki/görünür tile'lar DEM'i önce alıyor.

### Google Earth Rewrite Plan Faz Günlüğü
- [x] Faz 0 (Hazırlık) tamamlandı — Tarih: 2026-01-31 — Notlar: ParitySnapshot telemetry eklendi (frameTime, tileLoad, cache stats), --test-parity flag ve RunParityTest() senaryosu eklendi.
- [x] Faz 1 (Tile Addressing) tamamlandı — Tarih: 2026-01-31 — Notlar: TileKey/QuadKey/TileBounds altyapısı doğrulandı, BuildTileUrl -> TileUrlGenerator refactor edildi, duplicate ExpandUrlTemplate silindi.
- [x] Faz 2 (Scheduler) tamamlandı — Tarih: 2026-01-31 — Notlar: TileScheduler (Priority Queue, Decode Split, Limits) implemente edildi. TileLoadState makinesi hazır.
- [x] Faz 2.5 (Integration) tamamlandı — Tarih: 2026-01-31 — Notlar: GlobeEngine::SyncRasterTiles refactor edildi. TileScheduler ve GlobeTileFetcher entegre edildi. Base raster artık Scheduler kullanıyor.
- [x] Faz 3 (SSE LOD) tamamlandı — Tarih: 2026-01-31 — Notlar: ComputeTileSSE analitik formüle (Distance/Error) taşındı. Horizon Culling eklendi. ProjectToScreen kullanımı kaldırıldı.
- [x] Faz 4 (Terrain Mesh + Tile Stitch) tamamlandı — Tarih: 2026-02-02 — Notlar: Dynamic Skirt planı iptal edildi. Yerine P0.1-P0.3 kapsamındaki Edge Stitching (kenar dikişi) ve Re-stitch on Arrival mekanizmaları implemente edildi. Seam-free terrain sağlandı.
- [x] Faz 5 (Asset Loader) tamamlandı — Tarih: 2026-01-31 — Notlar: ImageDecoder ayrıştırıldı ve decode işlemi worker thread'e taşındı. GlobeTileFetcher ve TileScheduler entegrasyonu tamamlandı.
- [x] Faz 6 (Render Pipeline) tamamlandı — Tarih: 2026-01-31 — Notlar: Render logic Run döngüsünden Impl::Render ve alt metodlara (RenderTiles, RenderAtmosphere vb.) ayrıştırıldı. Shader kaynakları helper fonksiyonlara taşındı.
- [x] Faz 7 (JS Glue/Label) tamamlandı — Tarih: 2026-01-31 — Notlar: LabelManager eklendi. ImGui tabanlı text rasterization ile 3D->2D projeksiyonlu label rendering sağlandı.
- [x] Faz 8 (Stabilizasyon) tamamlandı — Tarih: 2026-01-31 — Notlar: PARITY_REPORT güncellendi, stabilite review tamamlandı, bilinen eksikler (vector styling, advanced picking) dokümante edildi. TileScheduler eviction/pending cleanup düzeltildi, TilePyramid LatLonToECEF ölçek uyumu (GLOBE_RADIUS_K) doğrulandı.
- [x] Faz 9 (Bug Fixes) tamamlandı — Tarih: 2026-01-31 — Notlar: Kırmızı background (debug color) düzeltildi. Tile birleşim çizgileri (seams) için normal hesaplaması sphere-normal'e çekildi. Atmosfer kenar çizgileri (depth artifacts) için alpha mask (smoothstep) ve depth-test disable/enable logic eklendi.

### Faz 4D Özeti (2026-01-29)
Build: Başarılı (✓)

| Kategori | Değişiklik |
|----------|------------|
| **Arcball Inertia** | Velocity-based → arcball-based (RotateByAngle) |
| **onGlobe Gate** | 3D modda globe dışında drag başlamaz |
| **Segment Tracking** | lastSegmentStart/End, lastDragMoveTime |
| **StopAnimation** | Mouse down'da çağrılır (JS: Xk7IZX) |
| **Double-click** | Logical coords kullanır (HiDPI parity) |
| **FCamMode** | TURNING state inertia sırasında aktif |
| **Threshold** | altitude / (2 * GLOBE_MAX_DIST) (JS parity) |

### Faz 0 Özeti (2026-01-26)
Build: Başarılı (✓)

| Kategori | Durum |
|----------|-------|
| **Value Object/Array** | ✓ Tamamlandı |
| **Return type düzeltmeleri** | ✓ 6 API düzeltildi |
| **P0 blocker'lar** | ✓ GlobeVersion, Geometry, Mouse APIs |
| **Stabilizasyon** | ✓ decimal LOD, zoom clamp, north angle normalize |
| **QueryByScreen** | ✓ Feature object array |
| **SetCameraPos/FlyToPointDirect** | ✓ North angle → yaw dönüşümü |
| **JS-style keys** | ✓ lon/long/lng, CenterLong, NorthAng, vb. |

### Faz 0 Metrikleri (Kullanıcı Raporu)
- Implementasyon sayısı: ~54 API (önceki ~45)
- Parity skoru: ~35-40/100
  - Return Types: 12/15
  - Coverage: 10/40
  - Accuracy: 10/30

### Faz 1 Özeti (2026-01-26)
- Raster CRUD + görünürlük/opaklık/z-index API’leri tamamlandı.
- GeoJSON string parse + ObjectCreator tamamlandı.
- QueryByBBox artık feature object array döndürüyor.
- Mouse event kayıt/okuma/temizleme API’leri eklendi.

### Faz 2 İlerleme Notları (2026-01-26)
- Conversion API’leri (DMS/UTM/MGRS/GeoRef/Mercator) implement edildi.
- 3D point API’leri (`api_Get3DPoint*`, `api_GetCartesian3DPoint*`) implement edildi.
- Draw* API’leri (Draw3d*, DrawCircle, DrawIcon, DrawContextText) komut kaydı ile bağlandı.
- Event dispatch/trigger + callback registry eklendi.
- AGL için `SampleTerrainHeightMeters` DEM servis çağrısı eklendi (MESHN=5, tek tile cache).
- `drawCommands_` read/clear debug API’leri + max-length cap eklendi.
- DEM cache artık multi-tile (LRU) ve `GlobeConfig` parametreleriyle yönetiliyor.

### Faz 2 Özeti (2026-01-26)
Build: Başarılı (✓)

| Kategori | API Sayısı |
|----------|------------|
| **Callback registry** | 8 |
| **Conversion APIs** | 17 |
| **3D point APIs** | 8 |
| **Draw* APIs** | 24 |

### Toplam İmplemente Edilen API (Kullanıcı Raporu)
- Önceki: ~88
- Şimdi: ~145 API

### Parity Skoru (Kullanıcı Tahmini)
- Önceki: ~55-60/100
- Şimdi: ~70-75/100

### Opsiyonel Sonraki Adımlar
- DEM grid orientation doğrulama (bilinen lokasyonla test) ve gerekirse `demRowsNorthToSouth` flip.
- Kalan stub’ların gerçek implementasyonu.

### Faz 0 + Faz 1 İlerleme Özeti (Kullanıcı Raporu)
Build: Başarılı (✓)

| Faz | Kategori | API Sayısı |
|-----|----------|------------|
| **0** | Value Object/Array, return types, P0 blockers | ~15 |
| **0** | Stabilizasyon (LOD, zoom, north angle, cursor) | ~8 |
| **1** | Raster CRUD | 12 |
| **1** | Layer style defaults | 2 |
| **1** | GeoJSON pipeline | 2 |
| **1** | Query expansion | 1 |
| **1** | Mouse events | 3 |

### Toplam İmplemente Edilen API (Kullanıcı Raporu)
- Önceki: ~45
- Şimdi: ~88 API

### Parity Skoru (Kullanıcı Tahmini)
- Önceki: 15/100
- Şimdi: ~55-60/100

### Güncel Durum Snapshot (2026-02-05 - 3D Terrain FAZ 1-3 Complete)
Toplam API: 365
Override edilen: 185
Non-null implementasyon: 185
Stub (Value::Null): 180
Kapsam: %50.6
**Not:** 3D Terrain rendering için FAZ 1-3 tamamlandı. HeightmapManager GPU texture pipeline aktif, vertex shader displacement hazır, DEM timeout artırıldı (30s).
**Accepted Deviations:** SSE Threshold scale (~2.5%) & ActivationK model.

### Doküman Bazlı Gap Analizi (webglobe_api_docs) — 2026-02-01 (Audited)
- Dokümanlarda geçen API sayısı: 163
- API listesinde olup **implement edilmemiş** (doc referansı): 39
- Dokümanda geçen ama `api_list.json` içinde **olmayan** isimler: 14 (isim uyumsuzluğu/typo olabilir)

**Eksik API’ler (doc referansı var, implementasyon yok):**
```
api_AddCustomFont
api_AddImageOverlay
api_AddImageOverlayHeatmap
api_AddTotalLayers
api_CalcSunMoon
api_CanMoveLabelsByMouse
api_CanResetLabelsByMouse
api_CancelVisibilityAnalysis
api_ChangeImageOverlayURL
api_ChangeWMSOverlayURL
api_DeleteAllObjectBuffers
api_DeleteImageOverlay
api_DeleteObjectBufferByIndex
api_FindObjectBufferById
api_FindProfile
api_GetImageOverlay
api_GetLayerLimitsOfLoadedObjects
api_GetMagneticNorthAngle
api_GetObjectBuffer
api_GetReductionBoxSize
api_GetScreenAsDegree
api_LayerObjectToJSON
api_LineOfSight
api_LoadVectorLayerWhileScreenIsMoving
api_ObjectBufferCount
api_QueryByObject_InsideData
api_QueryByObject_OverlapData
api_ReTryAtVectorLayerTimeout
api_ReloadLayer
api_SetCameraPosChanged
api_SetCompositeLayerVisibility
api_SetImageOverlayColor
api_SetMaxOpenRasterCount
api_SetMilSymbol
api_SetReductionBoxSize
api_SetReductionPeriod
api_SetVectorLayerTimeOut
api_UpdateLayer
api_VisibilityAnalysis
```

## Phase 19 Plan: Advanced Features & Analysis Tools
- [x] Image Overlays: `api_AddImageOverlay`, `api_DeleteImageOverlay`, `api_ChangeImageOverlayURL`, `api_SetImageOverlayColor`.
- [x] Analysis Tools: `api_LineOfSight`, `api_FindProfile`, `api_VisibilityAnalysis`, `api_CalcSunMoon` (Core implemented, CalcSunMoon deferred).
- [x] Object Buffer Management: `api_DeleteAllObjectBuffers`, `api_FindObjectBufferById`, `api_GetObjectBuffer`.
- [ ] Miscellaneous: `api_GetMagneticNorthAngle`, `api_GetScreenAsDegree`, `api_UpdateLayer`.

## Phase 20 Plan: Cleanup & Optimization (Deferred)
- [ ] Layer Management: `api_AddTotalLayers`, `api_ReloadLayer`.
- [ ] Settings/Config: `api_SetReductionBoxSize`, `api_SetReductionPeriod`, `api_SetVectorLayerTimeOut`, `api_SetMaxOpenRasterCount`, `api_SetMilSymbol`, `api_SetCompositeLayerVisibility`.
- [ ] Advanced Interaction: `api_SetCameraPosChanged`, `api_LoadVectorLayerWhileScreenIsMoving`, `api_CanMoveLabelsByMouse`, `api_CanResetLabelsByMouse`.
- [ ] Advanced Query: `api_QueryByObject_InsideData`, `api_QueryByObject_OverlapData`, `api_GetLayerLimitsOfLoadedObjects`.

**Dokümanda olup `api_list.json` içinde olmayan isimler (kontrol gerekli):**
```
api_Add
api_CheckLayerDrawModeChanges
api_DeleteMouseEvents
api_GetDefaultLayerSelectedStyle
api_GetLayerSelectedList
api_LAYER_TYPE_CAS_LAYER
api_ResetAllLayerLabels
api_SetLayerData
api_SetLayerFlash
api_SetLayerSelectedList
api_SetLayerSelectionFlash
api_SetLayerSelectionOpacity
api_TurntoNorth
api_UpdateLayerData
```

**Doküman bazlı eksik yoğunluğu (unique referanslar):**
- WebKure_API_Documentation.md: 39
- vectorLayer: 25
- navigation: 17
- analysis: 5
- introduction: 3
- heatmap: 2
- language: 1

---

## Ek: Hızlı Referans

### Mevcut İmplementasyonlar (globe_api.cpp)

```
✓ api_GlobeIsValid
✓ api_GetCurrentLOD
✓ api_GetCurrentLODWithDecimal
✓ api_SetNavigationDist
✓ api_SetNavigationLOD
✓ api_SetMinNavigationLOD
✓ api_SetMaxNavigationLOD
✓ api_SetTiltAngle
✓ api_SetNorthAngle
✓ api_Set2DMode
✓ api_ZoomToLOD
✓ api_FlyToPoint
✓ api_FlyToPointDirect
✓ api_FlyToRegion
✓ api_FlyToRegionDirect
✓ api_GetCameraDist
✓ api_ScrW
✓ api_ScrH
✓ api_FPS
✓ api_Altitude
✓ api_CamZ
✓ api_NorthAngleDeg
✓ api_GetScreenCenterAsDegree
✓ api_GetCurrentLookInfo (JS key’ler eklendi)
✓ api_IsScreenMoving
✓ api_GetCurrentScale
✓ api_GetCurrentMinLOD
✓ api_GetCurrentWorldLimit (stub)
✓ api_GetCurrentWorldWH (stub)
✓ api_SetCameraPos
✓ api_LeaveCamera
✓ api_SetLockNorth (stub)
✓ api_SetContinuousRotation (stub)
✓ api_SetMinNavigationDist (stub)
✓ api_SetMaxNavigationDist (stub)
✓ api_GetNavigationSpeed
✓ api_AddLayer
✓ api_DeleteLayer
✓ api_DeleteLayers
✓ api_GetLayer
✓ api_GetLayerById
✓ api_LayerCount
✓ api_GetNewLayerId
✓ api_SetLayerOn
✓ api_GetLayerOn
✓ api_SetLayerOpacity
✓ api_GetLayerStyle
✓ api_LayerStyleChanged
✓ api_QueryByScreen
✓ api_QueryByBBox
✓ api_GetGeoFromScreenPoint
✓ api_GetScreenPointFromGeo
✓ api_CanPickPoint
✓ api_GetDefaultStyle
✓ api_GetDefaultLayerStyle
✓ api_GeoJSONToObjectArrData
✓ api_ObjectCreator
✓ api_SetMouseEvents
✓ api_GetMouseEvent
✓ api_ClearMouseEvents
✓ api_GlobeVersion
✓ api_GetCurrentGeometry
✓ api_SetGeometry
✓ api_GetMousePos
✓ api_GetMouseDeg
✓ api_GetGL
✓ api_AddRaster
✓ api_SetRasterService
✓ api_DeleteRaster
✓ api_GetRaster
✓ api_GetRasterById
✓ api_SetRasterONOFF
✓ api_GetRasterONOFF
✓ api_GetRasterONOFFById
✓ api_SetRasterOpacity
✓ api_GetRasterOpacity
✓ api_SetRasterZIndex
✓ api_GetRasterZIndex
✓ api_GetNewRasterId
✓ api_RasterCount
✓ api_GetMercatorPoint
✓ api_GetMercator2DPoint
✓ api_GetMercator3DPoint
✓ api_GetMercator3DPoints
✓ api_GetMercator3DPointsByGeoArr
✓ api_GetMercator3DPointsByGeoArr_SameZ
✓ api_GeoToDMS
✓ api_DMSToGeo
✓ api_GetUTMZone
✓ api_GeoToUTM
✓ api_UTMToGeo
✓ api_GeoToMGRS
✓ api_MGRSToGeo
✓ api_GeoToGeoRef
✓ api_GeoRefToGeo
✓ api_Get3DPoint
✓ api_Get3DPoints
✓ api_Get3DPointsByGeoArr
✓ api_Get3DPointsByGeoArr_SameZ
✓ api_GetCartesian3DPoint
✓ api_GetCartesian3DPoints
✓ api_GetCartesian3DPointsByGeoArr
✓ api_GetCartesian3DPointsByGeoArr_SameZ
✓ api_Draw3dDashedLine
✓ api_Draw3dDashedLineLoop
✓ api_Draw3dDashedLineLoopCurModelProjection
✓ api_Draw3dDashedLineStrip
✓ api_Draw3dDashedLineStripCurModelProjection
✓ api_Draw3dLine
✓ api_Draw3dLineCurModelProjection
✓ api_Draw3dLineLoop
✓ api_Draw3dLineLoopCurModelProjection
✓ api_Draw3dLineStrip
✓ api_Draw3dLineStripCurModelProjection
✓ api_Draw3dPolygon
✓ api_Draw3dPolygonCurModelProjection
✓ api_Draw3dPolygonStrip
✓ api_Draw3dPolygonStripCurModelProjection
✓ api_DrawBaseGlobeColor
✓ api_DrawCircle
✓ api_DrawCircleCurModelProjection
✓ api_DrawContextText
✓ api_DrawContextTextInScreen
✓ api_DrawContextTextMultiLine
✓ api_DrawIcon
✓ api_DrawIconCurProjection
✓ api_GetDrawCommands
✓ api_ClearDrawCommands
✓ api_SetCameraCallBack
✓ api_SetStatusBarCallBack
✓ api_SetUndoBuffersChangedEvent
✓ api_EditCallbackChanged (stub)
✓ api_EditCallbackCreator
✓ api_GetEditCallback
✓ api_DispatchEvent
✓ api_TriggerCallback
✓ api_AddWMSOverlay
✓ api_SetWMSOverlayColor
✓ api_DeleteWMSOverlay
✓ api_SetLang
✓ api_GlobeFree
✓ api_GetDirectPosNatural
✓ api_OrbitDistance
✓ api_GetNewObjectBufferId
✓ api_CreateObjectBuffer
✓ api_AddObjectBuffer
✓ api_DeleteObjectBufferById
✓ api_GetTotalLayersAsJSON
✓ api_GetZClient
✓ api_GetDefaultClusterStyle
✓ api_ZoomToPaperScale
✓ api_GetFlashPeriod
✓ api_SetFlashPeriod
```

### Stub İmplementasyonlar (Toplam: 229)

Tüm `globe_api_generated.cpp` dosyasındaki `Value::Null()` dönen fonksiyonlar.
