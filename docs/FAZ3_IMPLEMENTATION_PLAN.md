# Faz 3 - Performans Optimizasyonu Implementasyon Planı
## Horizon Culling + Weighted Scheduler + Adaptive LOD

**Hedef:** %40-50 tile sayısı azaltımı, p95 frame-time < 16.6ms  
**Süre:** 4-5 gün  
**Bağımlılık:** Faz 2A/2B stabil (QA'dan geçmiş)

---

## Bölüm 1: Horizon Culling (Gün 1-2)

### 1.1 Yeni Dosyalar

```
src/math/horizon_culler.h
src/math/horizon_culler.cpp
tests/horizon_culler_test.cpp
```

### 1.2 HorizonCuller Sınıfı

```cpp
namespace globe {

struct HorizonCullResult {
    bool visible = true;
    float horizonAngleRad = 0.0f;
    float tileAngleRad = 0.0f;
    float confidence = 1.0f;  // 0=kesin görünür, 1=kesin gizli
};

class HorizonCuller {
public:
    explicit HorizonCuller(double earthRadiusKm = 6371.0);
    
    // Tile visibility test
    HorizonCullResult TestTile(const glm::dvec3& tileCenterEcef,
                               double tileBoundingRadiusKm,
                               const glm::dvec3& cameraPosEcef) const;
    
    // Batch test for LOD selector
    void TestTilesBatch(const std::vector<TileKey>& keys,
                       const std::unordered_map<TileKey, Tile>& tiles,
                       const glm::dvec3& cameraPosEcef,
                       std::vector<bool>& outVisible) const;
    
    // Debug: get horizon distance
    double GetHorizonDistanceKm(double cameraAltitudeKm) const;
    
    // Statistics
    int GetLastTestedCount() const { return lastTestedCount_; }
    int GetLastCulledCount() const { return lastCulledCount_; }

private:
    double earthRadiusKm_;
    double earthRadiusKmSquared_;
    mutable int lastTestedCount_ = 0;
    mutable int lastCulledCount_ = 0;
};

} // namespace globe
```

### 1.3 Matematik Formülleri

```cpp
// src/math/horizon_culler.cpp

// Horizon distance from camera altitude
// d = sqrt(h * (2R + h))
double HorizonCuller::GetHorizonDistanceKm(double cameraAltitudeKm) const {
    if (cameraAltitudeKm <= 0.0) return 0.0;
    return std::sqrt(cameraAltitudeKm * (2.0 * earthRadiusKm_ + cameraAltitudeKm));
}

// Main visibility test
HorizonCullResult HorizonCuller::TestTile(const glm::dvec3& tileCenterEcef,
                                          double tileBoundingRadiusKm,
                                          const glm::dvec3& cameraPosEcef) const {
    const double camDistFromCenter = glm::length(cameraPosEcef);
    const double tileDistFromCenter = glm::length(tileCenterEcef);
    
    // Camera altitude above surface
    const double camAltitudeKm = camDistFromCenter - earthRadiusKm_;
    
    // Vector from camera to tile
    const glm::dvec3 camToTile = tileCenterEcef - cameraPosEcef;
    const double distCamToTile = glm::length(camToTile);
    
    // Fast path: camera is inside Earth (shouldn't happen but handle gracefully)
    if (camAltitudeKm <= 0.0) {
        return {true, 0.0f, 0.0f, 0.0f};  // Show everything
    }
    
    // Horizon angle (angle between camera->center and camera->horizon tangent)
    const double horizonAngle = std::acos(earthRadiusKm_ / camDistFromCenter);
    
    // Tile angle (angle between camera->center and camera->tile)
    const double tileAngle = std::acos(glm::dot(glm::normalize(cameraPosEcef),
                                                glm::normalize(camToTile)));
    
    // Add bounding radius margin to tile angle
    const double angularRadius = std::asin(std::min(1.0, tileBoundingRadiusKm / tileDistFromCenter));
    const double tileAngleWithMargin = tileAngle + angularRadius;
    
    // Visibility test
    bool visible = tileAngleWithMargin <= horizonAngle;
    
    // Confidence based on how close to horizon
    double confidence = 0.0;
    if (!visible) {
        confidence = std::min(1.0, (tileAngleWithMargin - horizonAngle) / 0.01);  // Small radian tolerance
    }
    
    return {
        visible,
        static_cast<float>(horizonAngle),
        static_cast<float>(tileAngle),
        static_cast<float>(confidence)
    };
}
```

### 1.4 LODSelector Entegrasyonu

```cpp
// src/scheduling/lod_selector.h

class LodSelector {
public:
    // Add horizon culling support
    void SetHorizonCulling(bool enable, std::shared_ptr<HorizonCuller> culler);
    
private:
    bool useHorizonCulling_ = false;
    std::shared_ptr<HorizonCuller> horizonCuller_;
};

// src/scheduling/lod_selector.cpp

void LodSelector::Traverse(const TileKey& key, const TraverseContext& ctx) {
    // Existing SSE test
    bool shouldRefine = ...;
    
    // NEW: Horizon culling test
    if (useHorizonCulling_ && horizonCuller_) {
        auto tileIt = ctx.tiles.find(key);
        if (tileIt != ctx.tiles.end()) {
            const auto& tile = tileIt->second;
            auto result = horizonCuller_->TestTile(tile.center, 
                                                   tile.boundingRadius / 1000.0,  // km
                                                   ctx.cameraPosEcef);
            if (!result.visible) {
                // Tile is below horizon - skip entirely
                return;
            }
        }
    }
    
    // Continue with existing logic
    ...
}
```

### 1.5 GlobeEngine Entegrasyonu

```cpp
// src/engine/globe_engine.cpp

void GlobeEngine::Initialize() {
    // Existing init
    ...
    
    // NEW: Initialize horizon culling
    if (config_.useHorizonCulling) {
        horizonCuller_ = std::make_shared<HorizonCuller>(config_.earthRadiusKm);
        lodSelector_->SetHorizonCulling(true, horizonCuller_);
    }
}
```

### 1.6 Config Güncellemesi

```cpp
// src/core/config.h

struct Config {
    // Existing options...
    
    // Faz 3: Horizon Culling
    bool useHorizonCulling = true;           // Enable horizon culling
    double horizonCullingMarginKm = 10.0;    // Safety margin for visibility
    bool horizonCullingDebug = false;        // Visualize culled tiles
};
```

---

## Bölüm 2: Weighted Scheduler (Gün 2-3)

### 2.1 Yeni Dosyalar

```
src/scheduling/weighted_tile_scheduler.h
src/scheduling/weighted_tile_scheduler.cpp
tests/weighted_scheduler_test.cpp
```

### 2.2 WeightedTileScheduler Sınıfı

```cpp
namespace globe {

// Priority weight factors
struct PriorityWeights {
    float screenSpaceError = 1.0f;      // SSE importance
    float distance = 0.5f;              // Distance from camera
    float lodLevel = 0.3f;              // Higher LOD = higher priority
    float timeInQueue = 0.1f;           // Aging factor
    float visibility = 0.8f;            // Screen-space visibility
};

struct WeightedTileRequest {
    TileKey key;
    float priorityScore = 0.0f;
    double queuedTime = 0.0;
    glm::vec2 screenPos;  // Normalized screen position [-1, 1]
    float screenSize;     // Screen-space size
};

class WeightedTileScheduler {
public:
    explicit WeightedTileScheduler(const PriorityWeights& weights);
    
    // Add request with priority calculation
    void RequestTile(const TileKey& key, 
                     const glm::dvec3& cameraPosEcef,
                     const glm::dmat4& viewProjMatrix,
                     double currentTime);
    
    // Get next N highest priority requests
    std::vector<TileKey> GetNextRequests(int count, double currentTime);
    
    // Cancel request
    void CancelRequest(const TileKey& key);
    
    // Update weights dynamically
    void SetWeights(const PriorityWeights& weights);
    
    // Statistics
    int GetQueueSize() const;
    float GetAveragePriority() const;

private:
    PriorityWeights weights_;
    std::unordered_map<TileKey, WeightedTileRequest> requests_;
    
    float CalculatePriority(const TileKey& key,
                           const glm::dvec3& cameraPosEcef,
                           const glm::dmat4& viewProjMatrix,
                           double currentTime) const;
};

} // namespace globe
```

### 2.3 Priority Hesaplama Formülü

```cpp
float WeightedTileScheduler::CalculatePriority(const TileKey& key,
                                               const glm::dvec3& cameraPosEcef,
                                               const glm::dmat4& viewProjMatrix,
                                               double currentTime) const {
    // 1. Screen-space error (smaller = higher priority)
    float sse = CalculateScreenSpaceError(key, cameraPosEcef, viewProjMatrix);
    float ssePriority = 1.0f / (1.0f + sse);
    
    // 2. Distance from camera (closer = higher priority)
    float distance = CalculateDistanceToTile(key, cameraPosEcef);
    float distancePriority = 1.0f / (1.0f + distance / 1000.0f);  // Normalize by 1000km
    
    // 3. LOD level (higher detail = higher priority)
    float lodPriority = static_cast<float>(key.level) / 20.0f;  // Max level 20
    
    // 4. Time in queue (aging factor)
    auto it = requests_.find(key);
    float timeInQueue = (it != requests_.end()) ? 
                        static_cast<float>(currentTime - it->second.queuedTime) : 0.0f;
    float timePriority = std::min(1.0f, timeInQueue / 5.0f);  // Max 5 seconds
    
    // 5. Screen-space visibility (center of screen = higher priority)
    glm::vec2 screenPos = ProjectToScreen(key, viewProjMatrix);
    float visibilityPriority = 1.0f - glm::length(screenPos) / std::sqrt(2.0f);
    
    // Weighted sum
    float totalPriority = 
        weights_.screenSpaceError * ssePriority +
        weights_.distance * distancePriority +
        weights_.lodLevel * lodPriority +
        weights_.timeInQueue * timePriority +
        weights_.visibility * visibilityPriority;
    
    return totalPriority;
}
```

### 2.4 TileScheduler Entegrasyonu

```cpp
// src/scheduling/tile_scheduler.h

class TileScheduler {
public:
    void SetUseWeightedScheduler(bool enable);
    
private:
    bool useWeightedScheduler_ = false;
    std::unique_ptr<WeightedTileScheduler> weightedScheduler_;
};
```

---

## Bölüm 3: Adaptive LOD (Gün 3-4)

### 3.1 Yeni Dosyalar

```
src/scheduling/adaptive_lod_selector.h
src/scheduling/adaptive_lod_selector.cpp
tests/adaptive_lod_test.cpp
```

### 3.2 AdaptiveLODSelector Sınıfı

```cpp
namespace globe {

struct TerrainVariance {
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    float variance = 0.0f;  // Height variance (roughness)
};

class AdaptiveLodSelector {
public:
    struct Config {
        float baseSSEThreshold = 2.0f;           // Base SSE threshold
        float varianceWeight = 0.5f;             // How much variance affects LOD
        float heightRangeWeight = 0.3f;          // How much height range affects LOD
        int maxExtraLevels = 2;                  // Max extra LOD levels for rough terrain
    };
    
    explicit AdaptiveLodSelector(const Config& config);
    
    // Calculate desired LOD based on terrain characteristics
    int CalculateDesiredLod(const TileKey& key,
                           float baseSSE,
                           const TerrainVariance& variance) const;
    
    // Adjust SSE threshold dynamically
    float AdjustThreshold(float baseThreshold, const TerrainVariance& variance) const;
    
    // Update variance data from DEM
    void UpdateTerrainVariance(const TileKey& key, const TerrainVariance& variance);

private:
    Config config_;
    std::unordered_map<TileKey, TerrainVariance> varianceCache_;
};

} // namespace globe
```

### 3.3 DEM Entegrasyonu

```cpp
// src/io/dem_manager.h

class DemManager {
public:
    // NEW: Calculate terrain variance for a tile
    TerrainVariance CalculateVariance(const TileKey& key) const;
};
```

---

## Bölüm 4: Test Planı

### 4.1 Unit Testler

```cpp
// tests/horizon_culler_test.cpp

TEST(HorizonCuller, TileBelowHorizonIsCulled) {
    HorizonCuller culler(6371.0);  // Earth radius
    
    // Camera at 1000km altitude, looking at equator
    glm::dvec3 cameraPos(6671.0, 0.0, 0.0);  // km
    
    // Tile on opposite side of Earth
    glm::dvec3 tileCenter(-6371.0, 0.0, 0.0);  // km
    double tileRadius = 100.0;  // km
    
    auto result = culler.TestTile(tileCenter, tileRadius, cameraPos);
    
    EXPECT_FALSE(result.visible);
    EXPECT_GT(result.confidence, 0.9f);  // High confidence
}

TEST(HorizonCuller, TileAboveHorizonIsVisible) {
    HorizonCuller culler(6371.0);
    
    // Camera and tile close together
    glm::dvec3 cameraPos(6371.0 + 0.1, 0.0, 0.0);  // 100m altitude
    glm::dvec3 tileCenter(6371.0, 100.0, 0.0);  // Nearby tile
    
    auto result = culler.TestTile(tileCenter, 10.0, cameraPos);
    
    EXPECT_TRUE(result.visible);
}
```

### 4.2 Entegrasyon Testleri

```gherkin
Feature: Faz 3 Performance Optimizations

Scenario: Horizon culling reduces tile count
  Given camera at 1000km altitude
  And 1000 tiles in view frustum
  When horizon culling is enabled
  Then less than 600 tiles are rendered
  And performance improvement > 30%

Scenario: Weighted scheduler prioritizes visible tiles
  Given 100 tile requests queued
  And camera looking at tile A
  When scheduler picks next 10 tiles
  Then tile A is in the first 10
  And average priority decreases with queue position

Scenario: Adaptive LOD increases detail for mountains
  Given tile with high variance (mountains)
  And tile with low variance (plains)
  When LOD selection runs
  Then mountain tile gets +1 or +2 LOD levels
  And plain tile uses base LOD
```

---

## Bölüm 5: Implementasyon Adımları

### Gün 1: Horizon Culling
- [ ] 09:00 - HorizonCuller sınıfı implementasyonu
- [ ] 12:00 - Unit testler (4 test)
- [ ] 14:00 - LODSelector entegrasyonu
- [ ] 16:00 - GlobeEngine entegrasyonu
- [ ] 18:00 - Manual test + debug görselleştirme

### Gün 2: Weighted Scheduler (Part 1)
- [ ] 09:00 - WeightedTileScheduler sınıfı
- [ ] 13:00 - Priority hesaplama formülleri
- [ ] 15:00 - Unit testler
- [ ] 17:00 - TileScheduler entegrasyonu

### Gün 3: Weighted Scheduler (Part 2) + Adaptive LOD
- [ ] 09:00 - Weighted scheduler tuning
- [ ] 12:00 - AdaptiveLODSelector sınıfı
- [ ] 15:00 - DEM entegrasyonu
- [ ] 17:00 - Unit testler

### Gün 4: Entegrasyon ve Optimizasyon
- [ ] 09:00 - Tüm bileşenlerin birlikte testi
- [ ] 13:00 - Performance profiling
- [ ] 15:00 - Bottleneck analizi ve optimizasyon
- [ ] 17:00 - Integration testler

### Gün 5: QA ve Dokümantasyon
- [ ] 09:00 - QA test senaryoları
- [ ] 13:00 - Dokümantasyon güncelleme
- [ ] 15:00 - AGENTS.md güncelleme
- [ ] 17:00 - Final review

---

## Bölüm 6: Başarı Kriterleri

### Metrikler
| Metrik | Hedef | Ölçüm Metodu |
|--------|-------|--------------|
| Tile Count Azalımı | %40-50 | Rendered tile counter |
| Frame Time (p95) | < 16.6ms | GPU timer queries |
| Memory Kullanımı | < 2GB | OS memory tracker |
| First Render Time | < 200ms | Manual stopwatch |

### QA Onayı
- [ ] 30 dakika stress test (hızlı kamera hareketleri) - crash yok
- [ ] Horizon culling visual debug - doğru tile'lar atılıyor
- [ ] Weighted scheduler - öncelikli tile'lar önce yükleniyor
- [ ] Adaptive LOD - dağlık bölgelerde daha detaylı

---

## Riskler ve Mitigasyonlar

| Risk | Olasılık | Etki | Mitigasyon |
|------|----------|------|------------|
| Horizon culling yanlış tile atma | Orta | Yüksek | Güvenlik marjı + debug görselleştirme |
| Weighted scheduler açlık | Düşük | Orta | Aging faktörü + minimum priority |
| Adaptive LOD flickering | Orta | Orta | Hysteresis (geçiş gecikmesi) ekleme |
| Performans regresyonu | Düşük | Yüksek | A/B test, feature flag ile rollout |

---

## Sonraki Adımlar

Bu plan onaylandığında:
1. Yeni branch oluştur: `feature/faz3-performance-optimization`
2. Gün 1 görevlerine başla
3. Her gün sonunda test raporu güncelle
4. Bloklayıcı sorunları hemen raporla
