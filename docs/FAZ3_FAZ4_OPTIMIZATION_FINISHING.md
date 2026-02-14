# Faz 3 ve 4 - Optimizasyon ve Görsel Finisaj

> **Faz 3 Hedef:** Horizon Culling, Weighted Scheduler, SSE+Varyans LOD  
> **Faz 4 Hedef:** Görsel finisaj (dither crossfade, GPU de-kuantizasyon)  
> **Süre:** 5-7 gün  
> **Öncelik:** P1 (Performans) / P2 (Kalite)

---

## Faz 3A: Horizon Culling Entegrasyonu

### 3.1.1 Teori

**Horizon Culling:** Dünya küresi arkasında kalan tile'ları (ufkun altında) erken safhada atma.

**Matematik:**
```
Camera ----> Horizon (teğet noktası)
     \
      \     Tile (ufkun arkasında = at)
       \
        Dünya merkezi
```

```cpp
// Horizon distance formülü
// h = camera height above surface
// R = earth radius
// horizon_distance = sqrt(h * (2*R + h))

// Tile'ın görünürlük testi:
// 1. Camera'dan tile center'a vektör
// 2. Camera'dan dünya merkezine vektör
// 3. İki vektör arası açı
// 4. Açı > horizon açısı ise görünür
```

### 3.1.2 Implementasyon

```cpp
// src/math/frustum.h
struct HorizonCullResult {
    bool visible;
    float horizonAngleDeg;
    float tileAngleDeg;
};

class HorizonCuller {
public:
    explicit HorizonCuller(double earthRadiusKm);
    
    // Test if tile is above horizon
    HorizonCullResult TestTile(const Tile& tile, const glm::dvec3& cameraPosEcef) const;
    
    // Batch test multiple tiles
    std::vector<bool> TestTiles(const std::vector<Tile*>& tiles, 
                                const glm::dvec3& cameraPosEcef) const;
    
    // Get horizon distance for current altitude
    double GetHorizonDistanceKm(const glm::dvec3& cameraPosEcef) const;
    
private:
    double earthRadiusKm_;
    double earthRadiusKmSquared_;
};
```

```cpp
// src/math/frustum.cpp
HorizonCullResult HorizonCuller::TestTile(const Tile& tile, 
                                          const glm::dvec3& cameraPosEcef) const {
    const glm::dvec3 toTile = tile.center - cameraPosEcef;
    const double distToTile = glm::length(toTile);
    
    // Camera altitude above surface
    const double camDistFromCenter = glm::length(cameraPosEcef);
    const double altitudeKm = camDistFromCenter - earthRadiusKm_;
    
    // Horizon distance
    const double horizonDist = GetHorizonDistanceKm(cameraPosEcef);
    
    // Fast rejection: if tile is beyond horizon distance and behind camera
    // This is a conservative test
    if (distToTile > horizonDist) {
        // Check if tile is on the "far side" of the horizon
        const double tileDotCam = glm::dot(glm::normalize(toTile), 
                                           glm::normalize(cameraPosEcef));
        
        // If tile is pointing away from camera (opposite to earth center vector)
        // and beyond horizon, it's likely occluded
        if (tileDotCam < -0.1) {  // Small tolerance
            return {false, 0.0f, 0.0f};
        }
    }
    
    // More precise test: geometric horizon angle
    const double horizonAngle = std::acos(earthRadiusKm_ / camDistFromCenter);
    const double tileAngle = std::acos(glm::dot(glm::normalize(cameraPosEcef), 
                                                 glm::normalize(toTile)));
    
    const bool visible = tileAngle < (horizonAngle + 0.1);  // Small tolerance
    
    return {
        visible,
        static_cast<float>(glm::degrees(horizonAngle)),
        static_cast<float>(glm::degrees(tileAngle))
    };
}

double HorizonCuller::GetHorizonDistanceKm(const glm::dvec3& cameraPosEcef) const {
    const double camDistFromCenter = glm::length(cameraPosEcef);
    const double altitudeKm = camDistFromCenter - earthRadiusKm_;
    return std::sqrt(altitudeKm * (2.0 * earthRadiusKm_ + altitudeKm));
}
```

### 3.1.3 TilePyramid Entegrasyonu

```cpp
// src/scheduling/tile_pyramid.cpp
LodSelection TilePyramid::Select(...) {
    // ... mevcut LOD selection ...
    
    // Add horizon culling pass
    if (!config_.disableHorizonCull) {
        HorizonCuller culler(EARTH_RADIUS_KM);
        
        std::unordered_set<TileKey> visibleSet;
        visibleSet.reserve(selection.leafSet.size());
        
        int culledCount = 0;
        for (const auto& key : selection.leafSet) {
            auto it = tiles.find(key);
            if (it == tiles.end()) continue;
            
            auto result = culler.TestTile(it->second, cameraPos);
            if (result.visible) {
                visibleSet.insert(key);
            } else {
                culledCount++;
            }
        }
        
        // Replace leaf set with horizon-culled version
        selection.leafSet = std::move(visibleSet);
        
        // Telemetry
        debugStats_.horizonCulledCount = culledCount;
    }
    
    return selection;
}
```

---

## Faz 3B: Weighted Request Scheduler

### 3.2.1 Priority Class Sistemi

```cpp
// src/io/download_types.h
enum class RequestPriorityClass : uint8_t {
    Critical = 0,     // Camera altındaki tile (immediate)
    Terrain = 1,      // DEM verisi (yüksek)
    MeshNear = 2,     // Yakın RockMesh (yüksek)
    Visible = 3,      // Ekrandaki tile (normal-yüksek)
    MeshFar = 4,      // Uzak RockMesh (normal)
    Prefetch = 5,     // Ön yükleme (düşük)
    Background = 6    // Arka plan (en düşük)
};

struct WeightedRequest {
    TileKey key;
    RequestPriorityClass priorityClass;
    float screenSpaceScore;   // 0-1, camera merkezine yakınlık
    float distanceKm;         // Camera mesafesi
    float sseScore;           // Screen-space error
    double finalWeight;       // Hesaplanmış ağırlık
    
    bool operator<(const WeightedRequest& other) const {
        // Higher weight = higher priority (for priority_queue)
        return finalWeight < other.finalWeight;
    }
};
```

### 3.2.2 Ağırlık Hesaplama

```cpp
// src/scheduling/tile_scheduler.cpp
double ComputeRequestWeight(const WeightedRequest& req, 
                           const SchedulerConfig& config) {
    // Base weight from priority class
    static const double classWeights[] = {
        10000.0,  // Critical
        1000.0,   // Terrain
        500.0,    // MeshNear
        100.0,    // Visible
        50.0,     // MeshFar
        10.0,     // Prefetch
        1.0       // Background
    };
    
    double weight = classWeights[static_cast<int>(req.priorityClass)];
    
    // Screen space centrality bonus (0-1 range)
    weight *= (1.0 + req.screenSpaceScore * 2.0);
    
    // Distance penalty (closer = higher priority)
    if (req.distanceKm < 100.0) {
        weight *= 2.0;  // Within 100km
    } else if (req.distanceKm < 500.0) {
        weight *= 1.5;
    } else if (req.distanceKm > 5000.0) {
        weight *= 0.5;
    }
    
    // SSE bonus (higher SSE = more important to load)
    weight *= (1.0 + req.sseScore);
    
    // Age bonus (waiting longer = slight priority boost)
    // This prevents starvation
    
    return weight;
}
```

### 3.2.3 RockMeshManager Entegrasyonu

```cpp
// src/rendering/rockmesh_manager.cpp
void RockMeshManager::UpdateVisibleQuadKeys(
    const std::vector<TileKey>& visibleLeaves,
    const glm::dvec3& cameraPosEcef) {
    
    for (const auto& key : visibleLeaves) {
        // Convert to quadkey
        std::string nodeKey = TileKeyToNodeKey(key);
        
        // Calculate distance
        glm::dvec3 tileCenter = TileCenterWorldECEF(key);
        double distanceKm = glm::length(tileCenter - cameraPosEcef);
        
        // Determine priority class
        RequestPriorityClass priorityClass;
        if (distanceKm < 5.0) {
            priorityClass = RequestPriorityClass::MeshNear;
        } else if (distanceKm < 50.0) {
            priorityClass = RequestPriorityClass::Visible;
        } else {
            priorityClass = RequestPriorityClass::MeshFar;
        }
        
        // Screen space score (simplified)
        float screenScore = 1.0f - std::min(1.0f, static_cast<float>(distanceKm / 100.0f));
        
        // Create weighted request
        WeightedRequest req;
        req.key = key;
        req.priorityClass = priorityClass;
        req.screenSpaceScore = screenScore;
        req.distanceKm = distanceKm;
        req.sseScore = 1.0f;  // Simplified
        req.finalWeight = ComputeRequestWeight(req, config_);
        
        pendingRequests_.push(req);
    }
}
```

---

## Faz 3C: SSE + Varyans LOD

### 3.3.1 Height Varyans Hesaplama

```cpp
// src/rendering/heightmap_manager.cpp (veya DEM manager)
float ComputeHeightVariance(const std::vector<float>& elevationGrid, 
                           int gridSize) {
    if (elevationGrid.empty()) return 0.0f;
    
    // Compute mean
    double sum = 0.0;
    for (float h : elevationGrid) {
        sum += h;
    }
    double mean = sum / elevationGrid.size();
    
    // Compute variance
    double variance = 0.0;
    for (float h : elevationGrid) {
        double diff = h - mean;
        variance += diff * diff;
    }
    variance /= elevationGrid.size();
    
    return static_cast<float>(variance);
}
```

### 3.3.2 Adaptive LOD Threshold

```cpp
// src/scheduling/lod_selector.cpp
float LodSelector::ComputeAdaptiveThreshold(
    const Tile& tile, 
    float baseSseThreshold,
    const std::vector<float>& demData) {
    
    float variance = ComputeHeightVariance(demData, demMeshN_);
    
    // Normalize variance (0 = flat, 1 = mountainous)
    // Typical variance ranges:
    // - Ocean: < 1m^2
    // - Flat land: 1-10m^2  
    // - Hills: 10-100m^2
    // - Mountains: 100-1000m^2
    const float maxExpectedVariance = 1000.0f;
    float normalizedVariance = std::min(1.0f, variance / maxExpectedVariance);
    
    // High variance = need more detail (lower threshold)
    // Low variance = less detail needed (higher threshold)
    float varianceFactor = 1.0f - (normalizedVariance * 0.5f);
    
    // Additional tilt-based adjustment (oblique views need more detail)
    float tiltFactor = 1.0f + (tiltDegrees_ / 90.0f) * 0.3f;
    
    return baseSseThreshold * varianceFactor * tiltFactor;
}
```

---

## Faz 4: Görsel Finisaj

### 4.1 Stochastic Dithering Crossfade

### 4.1.1 Bayer Dither Pattern

```glsl
// shaders/crossfade_dither.glsl
#version 330 core

uniform sampler2D uTextureOld;
uniform sampler2D uTextureNew;
uniform float uCrossfadeT;  // 0 = old, 1 = new
uniform bool uUseDither;

in vec2 vTexCoord;
out vec4 FragColor;

// 4x4 Bayer matrix (normalized to 0-1)
const float bayer4x4[16] = float[](
    0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0, 4.0/16.0, 14.0/16.0, 6.0/16.0,
    3.0/16.0,  11.0/16.0, 1.0/16.0, 9.0/16.0,
    15.0/16.0, 7.0/16.0, 13.0/16.0, 5.0/16.0
);

float GetBitherThreshold(vec2 fragCoord) {
    int x = int(mod(fragCoord.x, 4.0));
    int y = int(mod(fragCoord.y, 4.0));
    return bayer4x4[x + y * 4];
}

void main() {
    vec4 oldColor = texture(uTextureOld, vTexCoord);
    vec4 newColor = texture(uTextureNew, vTexCoord);
    
    if (uUseDither) {
        // Stochastic dither
        float threshold = GetBitherThreshold(gl_FragCoord.xy);
        
        // Blend based on threshold vs crossfade
        // This creates a noise pattern that transitions
        float blend = step(threshold, uCrossfadeT);
        
        // Optional: smooth the transition slightly
        float smoothBlend = smoothstep(threshold - 0.1, threshold + 0.1, uCrossfadeT);
        
        FragColor = mix(oldColor, newColor, smoothBlend);
    } else {
        // Standard alpha blend
        FragColor = mix(oldColor, newColor, uCrossfadeT);
    }
}
```

### 4.1.2 Overdraw Azaltma

```cpp
// tests/crossfade_overdraw_test.cpp
TEST(Crossfade, DitherVsAlphaOverdraw) {
    // Render scene with alpha crossfade
    // Count overdraw (depth test failures)
    
    // Render scene with dither crossfade
    // Count overdraw
    
    // Dither should have less overdraw (early-z optimization works better)
    EXPECT_LT(ditherOverdraw, alphaOverdraw * 0.9f);
}
```

### 4.2 GPU De-kuantizasyon

### 4.2.1 Vertex Format Değişimi

```cpp
// src/io/providers/rocktree_node_data_parser.cpp
// Yeni vertex format
struct QuantizedVertex {
    int16_t x;      // Quantized X (tile-relative)
    int16_t y;      // Quantized Y (tile-relative)
    int16_t height; // Quantized height
    int16_t normalX; // Normal (oct-encoded, optional)
    int16_t normalY;
    uint16_t u;     // Texture UV
    uint16_t v;
};

// Vertex buffer stride: 14 bytes (vs 36 bytes for float32)
// Memory saving: ~60%
```

### 4.2.2 GPU Decode Shader

```glsl
// shaders/rockmesh_quantized.glsl
#version 330 core

// Quantization parameters
uniform vec3 uQuantOriginECEFHi;
uniform vec3 uQuantOriginECEFLo;
uniform float uQuantScale;  // Meters per quantization unit

// Vertex attributes
in ivec2 aQuantPosition;    // int16
in int aQuantHeight;        // int16
in vec2 aQuantUV;           // uint16 normalized

out vec2 vUV;

// Dequantize position
vec3 DequantizePosition() {
    vec3 quant = vec3(
        float(aQuantPosition.x),
        float(aQuantPosition.y),
        float(aQuantHeight)
    );
    
    vec3 localPos = quant * uQuantScale;
    
    vec3 origin = uQuantOriginECEFHi + uQuantOriginECEFLo;
    return origin + localPos;
}

void main() {
    vec3 worldPos = DequantizePosition();
    
    // Apply view/projection
    gl_Position = uMVP * vec4(worldPos, 1.0);
    
    vUV = aQuantUV;  // Already normalized 0-1
}
```

### 4.2.3 Parser Değişimi

```cpp
// src/io/providers/rocktree_node_data_parser.cpp
void ParseQuantizedMesh(const NodeData& data, ParsedNodeData& out) {
    // Compute quantization parameters
    glm::dvec3 bboxSize = data.bboxMax - data.bboxMin;
    double maxExtent = glm::max(bboxSize.x, glm::max(bboxSize.y, bboxSize.z));
    
    // 16-bit signed int range: -32768 to 32767
    // Scale to fit bbox within this range
    float scale = maxExtent / 32767.0f;
    
    out.quantScale = scale;
    out.quantOrigin = (data.bboxMin + data.bboxMax) * 0.5;
    
    // Quantize vertices
    for (const auto& v : data.vertices) {
        glm::dvec3 relPos = glm::dvec3(v.x, v.y, v.z) - out.quantOrigin;
        
        QuantizedVertex qv;
        qv.x = static_cast<int16_t>(relPos.x / scale);
        qv.y = static_cast<int16_t>(relPos.y / scale);
        qv.height = static_cast<int16_t>(relPos.z / scale);
        
        // Normalize UV to uint16
        qv.u = static_cast<uint16_t>(v.u * 65535.0f);
        qv.v = static_cast<uint16_t>(v.v * 65535.0f);
        
        out.quantizedVertices.push_back(qv);
    }
}
```

---

## Test Planı

### 3.x Faz 3 Testleri

```cpp
// tests/horizon_culling_test.cpp
TEST(HorizonCulling, OccludedTilesCulled) {
    HorizonCuller culler(EARTH_RADIUS_KM);
    
    // Camera at 10km altitude, looking at horizon
    glm::dvec3 camPos(EARTH_RADIUS_KM + 10.0, 0, 0);
    
    // Tile on opposite side of earth (should be culled)
    Tile farTile;
    farTile.center = glm::dvec3(-EARTH_RADIUS_KM, 0, 0);
    
    auto result = culler.TestTile(farTile, camPos);
    EXPECT_FALSE(result.visible);
    
    // Tile just at horizon (might be visible)
    Tile horizonTile;
    horizonTile.center = glm::dvec3(0, EARTH_RADIUS_KM, 0);
    
    result = culler.TestTile(horizonTile, camPos);
    // Result depends on exact math, but should be visible or borderline
}

TEST(HorizonCulling, PerformanceImprovement) {
    // Create many tiles around the globe
    // Count how many are culled
    // Expect 40-50% culling at typical viewing angles
}
```

```cpp
// tests/priority_scheduler_test.cpp
TEST(PriorityScheduler, CenterProximityPriority) {
    WeightedScheduler scheduler;
    
    // Add two requests: one near center, one far
    scheduler.AddRequest({{}, RequestPriorityClass::Visible, 0.9f, 10.0f});
    scheduler.AddRequest({{}, RequestPriorityClass::Visible, 0.1f, 1000.0f});
    
    // Higher screen score should be processed first
    auto next = scheduler.GetNextRequest();
    EXPECT_FLOAT_EQ(next.screenSpaceScore, 0.9f);
}

TEST(PriorityScheduler, NoStarvation) {
    // Add many high priority requests
    // Add one low priority request
    // Ensure low priority eventually gets processed
}
```

```cpp
// tests/sse_lod_decision_test.cpp
TEST(SseLod, VarianceAdaptiveSplit) {
    // Mountain tile: high variance
    std::vector<float> mountainDem(17*17);
    // Fill with varied heights...
    
    // Ocean tile: low variance  
    std::vector<float> oceanDem(17*17, 0.0f);
    
    LodSelector selector;
    
    float mountainThreshold = selector.ComputeAdaptiveThreshold(
        mountainTile, 2.0f, mountainDem);
    float oceanThreshold = selector.ComputeAdaptiveThreshold(
        oceanTile, 2.0f, oceanDem);
    
    // Mountain should have lower threshold (more detail)
    EXPECT_LT(mountainThreshold, oceanThreshold);
}
```

### 4.x Faz 4 Testleri

```cpp
// tests/rocktree_visual_continuity_test.cpp
TEST(RocktreeVisualContinuity, SeamlessTransitions) {
    // Zoom sweep test
    // Verify no pops or flashes during LOD transitions
    // Measure pixel difference between frames
}

TEST(RocktreeVisualContinuity, DitherQuality) {
    // Compare alpha vs dither crossfade quality
    // Dither should have less "ghosting"
}
```

---

## Entegrasyon ve Checklist

### Config Güncellemeleri

```cpp
// src/core/config.h
struct Config {
    // ... mevcut ...
    
    // Faz 3
    bool useHorizonCulling = true;
    bool useWeightedScheduling = true;
    bool useAdaptiveLodVariance = true;
    
    // Faz 4
    bool useDitherCrossfade = false;  // Alpha daha kararlı
    bool useGpuDequantization = false;  // Memory vs CPU decode tradeoff
    
    // Tuning
    float varianceLodThreshold = 100.0f;  // m^2
    float horizonCullTolerance = 0.1f;    // radians
};
```

### Checklist

#### Faz 3 Checklist

- [ ] Horizon culling %40+ tile atıyor
- [ ] Horizon test geçiyor (ufkun arkası atılmalı)
- [ ] Weighted scheduler priority class'ları kullanıyor
- [ ] Screen space score doğru hesaplanıyor
- [ ] Priority scheduler test geçiyor
- [ ] SSE + varyans LOD dağlık/düz bölgelerde farklı davranıyor
- [ ] Adaptive LOD test geçiyor

#### Faz 4 Checklist

- [ ] Dither crossfade shader çalışıyor
- [ ] Overdraw test geçiyor (dither < alpha)
- [ ] GPU de-kuantizasyon shader çalışıyor
- [ ] Quantized vertex format doğru
- [ ] Memory usage ölçümü yapıldı (quantized %60 daha az)
- [ ] Visual continuity test geçiyor
- [ ] Tüm finisaj özellikleri feature-flag ile kapatabilir

---

## Özet

**Faz 3** performans optimizasyonlarına odaklanıyor:
- Horizon culling: %40-50 tile atma
- Weighted scheduler: Daha akıllı önceliklendirme
- Adaptive LOD: Dağlık/düz bölgelerde optimize tile count

**Faz 4** görsel kalite finisajına odaklanıyor:
- Dither crossfade: Daha iyi performans, az overdraw
- GPU de-kuantizasyon: Düşük memory footprint, daha fazla cache

Her iki faz da feature flag ile tamamen opsiyonel ve geri alınabilir.
