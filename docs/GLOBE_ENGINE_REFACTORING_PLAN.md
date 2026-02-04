# Globe Engine Refactoring Plan

> **Son Güncelleme:** 2026-02-04  
> **Durum:** Phase 0 + Dead Code Cleanup TAMAMLANDI ✅

## 📊 İlerleme Özeti

| Metrik | Önceki | Sonra | Değişim |
|--------|--------|-------|----------|
| globe_engine.cpp | 11,580 satır | 11,320 satır | **-260 satır** |
| Duplicate fonksiyonlar | 12+ | 0 | **Temizlendi** |
| Dead code blokları | 5+ | 0 | **Kaldırıldı** |
| Yeni shared header | - | tile_math.h | **Oluşturuldu** |

## Overview

`globe_engine.cpp` is currently 430KB (11,500+ lines) and handles too many responsibilities.
This document outlines a plan to refactor it into smaller, focused modules.

---

## 🚨 PHASE 0: TileLodSelector Activation (ÖNCELİKLİ)

### Problem Özeti
`TileLodSelector` sınıfı modern SSE-based LOD seçimi için yazılmış ancak **bypass edilmiş**.
Eski `BuildVisibleTileSets` fonksiyonu aktif kullanılıyor.

### Tespit Edilen TileLodSelector Bug'ları

#### Bug 1: SSE Base Error Yanlış
**Dosya:** `tile_lod_selector.cpp:146`
```cpp
double geometricError = TileGeometricError(key.level, config_.geometricError * GLOBE_RADIUS);
```
- `config_.geometricError` default 1.0 → yanlış ölçek
- Legacy sistem `EARTH_CIRCUMFERENCE / (2^z * 256)` kullanıyor
- **Fix:** `ComputeGeometricError(z)` fonksiyonunu kullan

#### Bug 2: Distance Hesabı Eksik
**Dosya:** `tile_lod_selector.cpp:141`
```cpp
double distance = glm::length(tileCenter - cameraPos);
```
- Tile center'a mesafe alıyor
- Legacy `distance - radius` yapıyor (bounding sphere'in en yakın noktası)
- **Fix:** `distance = max(1.0, distance - tileRadius)` kullan

#### Bug 3: Tilt Factor Eksik
- Yüksek kamera tiltlarında horizon yakını tile'lar gereksiz subdivide oluyor
- Legacy sistemde `tiltFactor = 1 - tilt/150` uygulanıyor
- **Fix:** `CalculateSSE` fonksiyonuna tiltFactor parametresi ekle

#### Bug 4: Fallback Logic Yetersiz
**Dosya:** `tile_lod_selector.cpp:247-262`
- Children ready değilse sadece required'a ekliyor
- Legacy sistemdeki `AreChildrenReady` + fallback render yok
- **Fix:** `isTileReady` callback'i + `AreChildrenReady` check ekle

### Karşılaştırma Tablosu

| Özellik | Legacy | TileLodSelector | Status |
|---------|--------|-----------------|--------|
| Geometric Error | Earth-based (meters) | Abstract base (wrong) | ❌ |
| Distance | closest point on sphere | center only | ❌ |
| Tilt Factor | ✅ Applied | ❌ Missing | ❌ |
| Fallback Logic | ✅ Full support | ❌ Basic | ❌ |
| Horizon Culling | Angle-based | Distance-based | ⚠️ |
| Frustum Culling | ✅ | ✅ | ✅ |
| TileKey usage | String-based | TileKey struct | ✅ |

### Phase 0 Adımları ✅ TAMAMLANDI

- [x] **0.1** TileLodSelector SSE hesabını legacy ile uyumlu hale getir
- [x] **0.2** Distance hesabına bounding sphere offset ekle
- [x] **0.3** Tilt factor desteği ekle  
- [x] **0.4** Fallback logic'i güçlendir (`AreChildrenReady` eklendi)
- [x] **0.5** globe_engine.cpp'de TileLodSelector'ı aktif et
- [x] **0.6** BuildVisibleTileSets ve tüm bağımlı fonksiyonlar **KALDIRILDI**

### Dead Code / Duplicate Cleanup ✅ TAMAMLANDI

**Kaldırılan Fonksiyonlar:**
- `IsTileReady()` - TileLodSelector callback'e taşındı
- `AreChildrenReady()` (globe_engine) - TileLodSelector'a taşındı
- `CollectVisibleTilesRecursive()` - TileLodSelector::TraverseTile() ile değiştirildi
- `CollectVisibleTilesRecursiveFixed()` - Artık kullanılmıyor
- `BuildVisibleTileSets()` - TileLodSelector::Select() ile değiştirildi
- `BuildVisibleTileSetsFixed()` - Artık kullanılmıyor
- `ShouldSubdivideTile()` - TileLodSelector içinde
- `ComputeGeometricError()` - tile_math.h'a taşındı
- `ComputeGeometricSSE()` - tile_math.h'a taşındı

**Yeni Oluşturulan: `src/tile_math.h`**
```
earth::GLOBE_RADIUS, GLOBE_RADIUS_K, EARTH_CIRCUMFERENCE
earth::Tile2Lon(), Tile2Lat()
earth::TileCenterNormal(), TileCenterWorld()
earth::TileAngularRadius(), TileBoundingRadius()
earth::ComputeGeometricError(), ComputeGeometricSSE()
earth::ComputeTileSseRatio()
```

---

## Current Responsibilities in globe_engine.cpp

1. **Tile Synchronization** (~1500 lines)
   - `SyncRasterTiles()`, `SyncRasterTilesPass3()`, `SyncLayerTiles()`, `SyncVectorTiles()`
   - Tile creation, loading, eviction

2. **Download Management** (~800 lines)
   - `WorkerLoop()`, `DemWorkerLoop()`
   - Download queue management
   - Result processing

3. **Rendering** (~1200 lines)
   - `Render()`, `RenderTiles()`, `RenderRasterOverlays()`, `RenderVectors()`
   - Shader management, GL state

4. **Camera & Navigation** (~600 lines)
   - Camera matrix calculation
   - Mouse/keyboard input handling
   - Animation updates

5. **DEM/Terrain** (~1500 lines)
   - Mesh stitching logic
   - Height sampling
   - DEM tile management

6. **UI (ImGui)** (~800 lines)
   - Debug panels
   - Statistics display

7. **Utilities** (~500 lines)
   - URL building
   - Coordinate conversions
   - Tile math

## Proposed Module Structure

```
src/
├── globe_engine.cpp          # Core engine, reduced to ~2000 lines
├── globe_engine.h            # Public API
├── globe_engine_impl.h       # Internal state (Impl struct)
│
├── tile_sync.cpp             # NEW: Tile synchronization logic
├── tile_sync.h
│
├── download_manager.cpp      # NEW: Download queue and workers
├── download_manager.h
│
├── globe_renderer.cpp        # NEW: Rendering pipeline
├── globe_renderer.h
│
├── dem_manager.cpp           # NEW: DEM/terrain handling
├── dem_manager.h
│
├── texture_manager.cpp       # DONE: Already extracted
├── texture_manager.h
│
├── tile_manager.cpp          # EXISTS: Expand with eviction logic
├── tile_manager.h
│
├── tile_scheduler.cpp        # EXISTS: Keep as-is
├── tile_scheduler.h
```

## Refactoring Priority

### Phase 0: TileLodSelector Activation ✅ DONE
- [x] Fix SSE calculation bugs (Earth-based geometric error)
- [x] Add tilt factor support
- [x] Improve fallback logic (AreChildrenReady)
- [x] Activate TileLodSelector in globe_engine.cpp
- [x] Deprecate BuildVisibleTileSets

### Phase 1: Low-Risk Extractions (DONE)
- [x] TextureManager extracted
- [x] Lock hierarchy documented
- [x] Deprecated code marked

### Phase 2: Download Manager
- [ ] Extract `WorkerLoop()` and download queue management
- [ ] Create `DownloadManager` class with:
  - `Enqueue()`, `Cancel()`, `ProcessResults()`
  - Internal thread pool
  - Priority queue management

### Phase 3: Tile Sync
- [ ] Extract `SyncRasterTiles()` family to `tile_sync.cpp`
- [ ] Reduce coupling to Impl struct
- [ ] Create clear interface for tile state updates

### Phase 4: DEM Manager
- [ ] Extract DEM stitching logic
- [ ] Extract `DemWorkerLoop()`
- [ ] Create `DemManager` class

### Phase 5: Renderer
- [ ] Extract `Render*()` functions
- [ ] Create `GlobeRenderer` class
- [ ] Manage shader programs internally

## Implementation Notes

### Dependency Order
When extracting modules, follow this order to minimize circular dependencies:
1. TextureManager (no deps) ✅
2. DownloadManager (depends on TextureManager)
3. TileSync (depends on DownloadManager)
4. DemManager (depends on TileSync)
5. GlobeRenderer (depends on all above)

### Interface Design
Each extracted module should:
- Have a clear, minimal public API
- Use dependency injection (pass managers via constructor)
- Not directly access `GlobeEngine::Impl`
- Use callbacks or events for cross-module communication

### Testing Strategy
- Add unit tests for each extracted module
- Create mock implementations for dependencies
- Verify no regression in rendering quality

## Estimated Effort

| Phase | Estimated Lines | Complexity | Risk |
|-------|-----------------|------------|------|
| Phase 1 | ~500 | Low | Low |
| Phase 2 | ~800 | Medium | Medium |
| Phase 3 | ~1500 | High | Medium |
| Phase 4 | ~1500 | High | High |
| Phase 5 | ~1200 | Medium | Medium |

Total: ~5500 lines to extract, reducing globe_engine.cpp to ~6000 lines.

## Success Criteria

1. No single source file > 3000 lines
2. Each module has single responsibility
3. Clear ownership of mutex locks per module
4. Unit tests for extracted modules
5. No performance regression
