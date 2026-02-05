# 3D Terrain Rendering Geliştirme Planı

**Tarih:** 2026-02-05  
**Hedef:** Google Earth benzeri 3D terrain görünümü  
**Tahmini Süre:** 2-3 hafta

## İmplementasyon Durumu

| Faz | Durum | Açıklama |
|-----|-------|----------|
| FAZ 1 | ✅ Tamamlandı | demMeshN=65, demHeightScale=2.5, meshSegments=64 |
| FAZ 2 | ✅ Tamamlandı | HeightmapManager oluşturuldu, GlobeEngine'e entegre |
| FAZ 3 | ✅ Tamamlandı | Vertex shader displacement uniforms eklendi |
| FAZ 4 | ⏳ Beklemede | TerrainPicker implementasyonu |
| FAZ 5 | ⏳ Beklemede | Progressive DEM loading |
| FAZ 6 | ✅ Tamamlandı | Build ve temel test |

---

## Özet

Bu plan, native_globe projesinde 3D terrain rendering'i Google Earth kalitesine yaklaştırmak için gerekli tüm değişiklikleri kapsar.

### Tespit Edilen Sorunlar
1. DEM grid çözünürlüğü çok düşük (17x17)
2. Height scale yetersiz (1.0x - dağlar görünmüyor)
3. GPU displacement yok (CPU-only mesh bake)
4. Terrain picking yok (sabit sphere intersection)
5. Asenkron DEM yüklemede "pop" efekti
6. Heightmap texture formatı yok

---

## FAZ 1: Temel Parametre İyileştirmeleri
**Süre:** 1 gün | **Risk:** Düşük | **Etki:** Yüksek

### 1.1 DEM Grid Çözünürlüğü Artırma

**Dosya:** `src/core/config.h`

```cpp
// Değişiklik öncesi:
int demMeshN = 17;

// Değişiklik sonrası:
int demMeshN = 65;  // 65x65 = 4225 samples per tile
```

**Gerekçe:** 17x17 çözünürlük dağları "kare piramit" gösteriyor. 65x65 daha pürüzsüz arazi sağlar.

### 1.2 Height Scale Varsayılanı Artırma

**Dosya:** `src/core/config.h`

```cpp
// Değişiklik öncesi:
double demHeightScale = 1.0;

// Değişiklik sonrası:
double demHeightScale = 2.0;  // 2x exaggeration (Google Earth default)
```

**Gerekçe:** Küresel ölçekte dağlar %0.14 (Everest/R_earth). 2-3x büyütme görsel etki sağlar.

### 1.3 DEM Cache Boyutu Artırma

**Dosya:** `src/core/config.h`

```cpp
// Değişiklik öncesi:
size_t demCacheSize = 256;

// Değişiklik sonrası:
size_t demCacheSize = 512;  // More tiles cached for smooth navigation
```

### 1.4 Mesh Segment Uyumu

**Dosya:** `src/core/config.h`

```cpp
// Değişiklik öncesi:
int meshSegments = 16;

// Değişiklik sonrası:
int meshSegments = 64;  // Match demMeshN-1 for best quality
```

### Doğrulama Kriterleri
- [ ] Uygulama hatasız derlenip çalışıyor
- [ ] DEM verisi yükleniyor (console log kontrol)
- [ ] Dağlar görünür derecede yüksek görünüyor
- [ ] Performans kabul edilebilir (>30 FPS)

---

## FAZ 2: GPU Heightmap Texture Sistemi
**Süre:** 3 gün | **Risk:** Orta | **Etki:** Yüksek

### 2.1 HeightmapTexture Struct Tanımlama

**Yeni dosya:** `src/rendering/heightmap_texture.h`

```cpp
#pragma once
#include <cstdint>
#include <vector>

namespace globe {

struct HeightmapTexture {
    uint32_t textureId = 0;
    int width = 0;
    int height = 0;
    float minHeight = 0.0f;  // Normalization için
    float maxHeight = 0.0f;
    bool valid = false;
};

class HeightmapManager {
public:
    // DEM verisinden GPU texture oluştur
    HeightmapTexture CreateFromDEM(const std::vector<double>& heights, 
                                    int gridSize,
                                    float minH, float maxH);
    
    // Texture sil
    void Release(HeightmapTexture& tex);
    
    // Upload queue (frame-budgeted)
    void QueueUpload(const TileKey& key, const DemGridData& data);
    void ProcessUploads(double budgetMs);
    
    // Get texture for tile
    bool GetTexture(const TileKey& key, HeightmapTexture& out) const;

private:
    std::unordered_map<TileKey, HeightmapTexture> cache_;
    // Upload queue...
};

} // namespace globe
```

### 2.2 DemManager'a Heightmap Texture Entegrasyonu

**Dosya:** `src/io/dem_manager.h` - Tile struct'a heightmap ekle

```cpp
struct Tile {
    // ... mevcut alanlar ...
    
    // Heightmap texture (GPU)
    uint32_t heightmapId = 0;
    float heightmapMinH = 0.0f;
    float heightmapMaxH = 0.0f;
    bool hasHeightmap = false;
};
```

### 2.3 R16F Texture Format Upload

**Dosya:** `src/rendering/heightmap_texture.cpp`

```cpp
HeightmapTexture HeightmapManager::CreateFromDEM(
    const std::vector<double>& heights, 
    int gridSize,
    float minH, float maxH
) {
    HeightmapTexture tex;
    tex.width = gridSize;
    tex.height = gridSize;
    tex.minHeight = minH;
    tex.maxHeight = maxH;
    
    // Normalize heights to [0, 1] and convert to float16
    std::vector<float> normalized(heights.size());
    float range = maxH - minH;
    if (range < 0.001f) range = 1.0f;
    
    for (size_t i = 0; i < heights.size(); ++i) {
        normalized[i] = static_cast<float>((heights[i] - minH) / range);
    }
    
    // Create R16F texture
    glGenTextures(1, &tex.textureId);
    glBindTexture(GL_TEXTURE_2D, tex.textureId);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, 
                 gridSize, gridSize, 0, 
                 GL_RED, GL_FLOAT, normalized.data());
    
    // Bilinear filtering for smooth interpolation
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    tex.valid = true;
    return tex;
}
```

### Doğrulama Kriterleri
- [ ] Heightmap texture başarıyla oluşturuluyor
- [ ] Memory leak yok (glDeleteTextures çağrılıyor)
- [ ] Upload budgeting çalışıyor (frame spike yok)

---

## FAZ 3: Vertex Shader Displacement
**Süre:** 3 gün | **Risk:** Orta | **Etki:** Çok Yüksek

### 3.1 Yeni Vertex Shader

**Dosya:** `src/rendering/shader_manager.h` - TILE_VERTEX_TERRAIN

```glsl
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uMVP;
uniform sampler2D uHeightmap;
uniform float uHeightScale;
uniform float uHeightMin;
uniform float uHeightMax;
uniform int uHasHeightmap;

out vec2 vTexCoord;
out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    vec3 pos = aPos;
    vec3 normal = aNormal;
    
    if (uHasHeightmap == 1) {
        // Sample heightmap
        float heightNorm = texture(uHeightmap, aTexCoord).r;
        float heightKm = mix(uHeightMin, uHeightMax, heightNorm) * uHeightScale;
        
        // Displace vertex radially outward
        vec3 radialDir = normalize(aPos);
        pos = aPos + radialDir * heightKm;
        
        // Recalculate normal from heightmap gradient
        vec2 texelSize = 1.0 / vec2(textureSize(uHeightmap, 0));
        float hL = texture(uHeightmap, aTexCoord - vec2(texelSize.x, 0)).r;
        float hR = texture(uHeightmap, aTexCoord + vec2(texelSize.x, 0)).r;
        float hD = texture(uHeightmap, aTexCoord - vec2(0, texelSize.y)).r;
        float hU = texture(uHeightmap, aTexCoord + vec2(0, texelSize.y)).r;
        
        // Tangent-space normal
        vec3 tangentNormal = normalize(vec3(hL - hR, hD - hU, 0.01));
        
        // Transform to world space (simplified - assumes radial up)
        normal = normalize(radialDir + tangentNormal * 0.5);
    }
    
    gl_Position = uMVP * vec4(pos, 1.0);
    vTexCoord = aTexCoord;
    vNormal = normal;
    vWorldPos = pos;
}
```

### 3.2 ShaderManager Güncelleme

**Dosya:** `src/rendering/shader_manager.h`

```cpp
enum class ShaderFlags : uint32_t {
    None        = 0,
    Wireframe   = 1 << 0,
    DebugSeams  = 1 << 1,
    NoLighting  = 1 << 2,
    DebugLOD    = 1 << 3,
    Terrain     = 1 << 4,  // YENİ: GPU terrain displacement
};
```

### 3.3 Uniform Location Cache

**Dosya:** `src/rendering/shader_manager.cpp`

```cpp
void ShaderManager::CacheUniformLocations(uint32_t program) {
    mvpLoc_ = glGetUniformLocation(program, "uMVP");
    texLoc_ = glGetUniformLocation(program, "uTexture");
    fadeLoc_ = glGetUniformLocation(program, "uFade");
    lodLevelLoc_ = glGetUniformLocation(program, "uLodLevel");
    
    // Terrain uniforms
    heightmapLoc_ = glGetUniformLocation(program, "uHeightmap");
    heightScaleLoc_ = glGetUniformLocation(program, "uHeightScale");
    heightMinLoc_ = glGetUniformLocation(program, "uHeightMin");
    heightMaxLoc_ = glGetUniformLocation(program, "uHeightMax");
    hasHeightmapLoc_ = glGetUniformLocation(program, "uHasHeightmap");
}
```

### 3.4 TileRenderer Güncelleme

**Dosya:** `src/rendering/tile_renderer.cpp`

```cpp
void TileRenderer::RenderTileWithTerrain(const Tile& tile, 
                                          uint32_t colorTexture,
                                          uint32_t heightmapTexture,
                                          float heightMin, float heightMax,
                                          float heightScale) {
    // Bind color texture to unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    
    // Bind heightmap to unit 1
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, heightmapTexture);
    
    // Set uniforms
    glUniform1i(shaderManager_.GetHeightmapLocation(), 1);
    glUniform1f(shaderManager_.GetHeightScaleLocation(), heightScale);
    glUniform1f(shaderManager_.GetHeightMinLocation(), heightMin);
    glUniform1f(shaderManager_.GetHeightMaxLocation(), heightMax);
    glUniform1i(shaderManager_.GetHasHeightmapLocation(), 1);
    
    // Draw
    glBindVertexArray(tile.vao);
    glDrawElements(GL_TRIANGLES, tile.indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
```

### Doğrulama Kriterleri
- [ ] Shader derleme hatası yok
- [ ] Heightmap ile terrain yükseliyor
- [ ] Normal hesaplaması doğru (ışıklandırma mantıklı)
- [ ] Heightmap olmayan tile'lar düz kalıyor

---

## FAZ 4: Terrain-Aware Picking
**Süre:** 2 gün | **Risk:** Orta | **Etki:** Yüksek

### 4.1 TerrainPicker Sınıfı

**Yeni dosya:** `src/core/terrain_picker.h`

```cpp
#pragma once
#include <glm/glm.hpp>

namespace globe {

class DemManager;

class TerrainPicker {
public:
    explicit TerrainPicker(DemManager* demManager);
    
    // Ray-terrain intersection
    // Returns true if hit, outPoint contains hit position
    bool Pick(const glm::dvec3& rayOrigin, 
              const glm::dvec3& rayDir,
              glm::dvec3& outPoint,
              int maxIterations = 64) const;
    
    // Get terrain height at lon/lat
    bool GetHeight(double lon, double lat, double& outHeightKm) const;

private:
    DemManager* demManager_;
    
    // Binary search along ray
    bool BinarySearchHit(const glm::dvec3& rayOrigin,
                         const glm::dvec3& rayDir,
                         double tMin, double tMax,
                         glm::dvec3& outPoint) const;
    
    // Check if point is below terrain
    bool IsBelowTerrain(const glm::dvec3& point) const;
};

} // namespace globe
```

### 4.2 TerrainPicker Implementasyonu

**Yeni dosya:** `src/core/terrain_picker.cpp`

```cpp
#include "terrain_picker.h"
#include "../io/dem_manager.h"
#include "../core/ellipsoid.h"
#include <cmath>

namespace globe {

TerrainPicker::TerrainPicker(DemManager* demManager)
    : demManager_(demManager) {}

bool TerrainPicker::GetHeight(double lon, double lat, double& outHeightKm) const {
    if (!demManager_) return false;
    
    // Try multiple LOD levels, finest first
    for (int level = 14; level >= 0; --level) {
        double heightM = 0.0;
        if (demManager_->SampleHeight(lon, lat, level, heightM)) {
            outHeightKm = heightM * 0.001;
            return true;
        }
    }
    return false;
}

bool TerrainPicker::IsBelowTerrain(const glm::dvec3& point) const {
    // Convert to lon/lat
    double r = glm::length(point);
    double lat = glm::degrees(std::asin(point.z / r));
    double lon = glm::degrees(std::atan2(point.y, point.x));
    
    double terrainHeightKm = 0.0;
    if (!GetHeight(lon, lat, terrainHeightKm)) {
        terrainHeightKm = 0.0;  // Fallback to sea level
    }
    
    double terrainRadius = EARTH_RADIUS_KM + terrainHeightKm;
    return r < terrainRadius;
}

bool TerrainPicker::Pick(const glm::dvec3& rayOrigin,
                          const glm::dvec3& rayDir,
                          glm::dvec3& outPoint,
                          int maxIterations) const {
    // First: intersect with bounding sphere (Earth + max terrain height)
    const double maxTerrainKm = 10.0;  // ~Everest
    const double boundingR = EARTH_RADIUS_KM + maxTerrainKm;
    
    double a = glm::dot(rayDir, rayDir);
    double b = 2.0 * glm::dot(rayOrigin, rayDir);
    double c = glm::dot(rayOrigin, rayOrigin) - boundingR * boundingR;
    double disc = b * b - 4.0 * a * c;
    
    if (disc < 0.0) return false;
    
    double sqrtD = std::sqrt(disc);
    double t1 = (-b - sqrtD) / (2.0 * a);
    double t2 = (-b + sqrtD) / (2.0 * a);
    
    double tMin = std::max(0.0, t1);
    double tMax = t2;
    
    if (tMax < 0.0) return false;
    
    return BinarySearchHit(rayOrigin, rayDir, tMin, tMax, outPoint);
}

bool TerrainPicker::BinarySearchHit(const glm::dvec3& rayOrigin,
                                     const glm::dvec3& rayDir,
                                     double tMin, double tMax,
                                     glm::dvec3& outPoint) const {
    // March along ray, find first below-terrain point
    const int steps = 32;
    double tBelow = -1.0;
    double tAbove = tMin;
    
    for (int i = 0; i <= steps; ++i) {
        double t = tMin + (tMax - tMin) * i / steps;
        glm::dvec3 p = rayOrigin + rayDir * t;
        
        if (IsBelowTerrain(p)) {
            tBelow = t;
            break;
        }
        tAbove = t;
    }
    
    if (tBelow < 0.0) return false;  // No hit
    
    // Binary search for exact intersection
    for (int i = 0; i < 16; ++i) {
        double tMid = (tAbove + tBelow) * 0.5;
        glm::dvec3 p = rayOrigin + rayDir * tMid;
        
        if (IsBelowTerrain(p)) {
            tBelow = tMid;
        } else {
            tAbove = tMid;
        }
    }
    
    outPoint = rayOrigin + rayDir * ((tAbove + tBelow) * 0.5);
    return true;
}

} // namespace globe
```

### 4.3 FlightController Entegrasyonu

**Dosya:** `src/camera/flight_controller.cpp`

```cpp
// pickCallback'i terrain-aware yap
// GlobeEngine::Init() içinde:
flightController_->SetPickCallback([this](double x, double y, glm::dvec3& outPoint) {
    // Önce terrain picker dene
    glm::dvec3 origin, dir;
    camera_->GetRay(x, y, config_.windowWidth, config_.windowHeight, origin, dir);
    
    if (terrainPicker_ && terrainPicker_->Pick(origin, dir, outPoint)) {
        return true;
    }
    
    // Fallback: sphere intersection
    return PickGlobe(x, y, outPoint);
});
```

### Doğrulama Kriterleri
- [ ] Dağlara tıklandığında doğru nokta seçiliyor
- [ ] Orbit pivot dağ zirvesinde doğru konumlanıyor
- [ ] Pan işlemi terrain'i takip ediyor
- [ ] Performans kabul edilebilir (<1ms per pick)

---

## FAZ 5: Progressive DEM Loading
**Süre:** 2 gün | **Risk:** Düşük | **Etki:** Orta

### 5.1 DEM Loading States

**Dosya:** `src/core/tile.h`

```cpp
enum class DemState : uint8_t {
    None,       // DEM istenmedi
    Pending,    // DEM istendi, bekleniyor
    Loaded,     // DEM yüklendi (CPU)
    Uploaded,   // Heightmap GPU'da
    Failed      // DEM yüklenemedi
};

struct Tile {
    // ... mevcut alanlar ...
    DemState demState = DemState::None;
};
```

### 5.2 Smooth Transition

**Dosya:** `src/rendering/tile_renderer.cpp`

```cpp
// DEM yüklenirken parent'ın DEM'ini kullan
void TileRenderer::RenderTileWithFallback(const Tile& tile, 
                                           const Tile* parentTile,
                                           float transitionAlpha) {
    if (tile.demState == DemState::Uploaded) {
        // Kendi heightmap'ini kullan
        RenderTileWithTerrain(tile, tile.textureId, tile.heightmapId, ...);
    } else if (parentTile && parentTile->demState == DemState::Uploaded) {
        // Parent heightmap'ini kullan (UV offset ile)
        RenderTileWithParentTerrain(tile, parentTile, transitionAlpha);
    } else {
        // Düz render (DEM yok)
        RenderTileFlat(tile);
    }
}
```

### 5.3 LOD-Aware DEM Request Priority

**Dosya:** `src/engine/globe_engine.cpp`

```cpp
// DEM request'lerini önceliklendir
for (const TileKey& key : selection.leaves) {
    // Leaf tile'lar en yüksek öncelik
    demManager_->Request(key, Priority::Urgent);
}

for (const TileKey& key : selection.required) {
    if (selection.leafSet.count(key) == 0) {
        // Ancestor tile'lar düşük öncelik
        demManager_->Request(key, Priority::Normal);
    }
}
```

### Doğrulama Kriterleri
- [ ] DEM yüklenirken "pop" yerine smooth transition
- [ ] Parent DEM fallback çalışıyor
- [ ] Priority sistem doğru öncelik veriyor

---

## FAZ 6: Test ve Doğrulama
**Süre:** 2 gün | **Risk:** Düşük | **Etki:** -

### 6.1 Test Lokasyonları

| Lokasyon | Lat | Lon | Beklenen |
|----------|-----|-----|----------|
| Everest | 27.9881 | 86.9250 | Belirgin zirve |
| Grand Canyon | 36.0544 | -112.1401 | Derin vadi |
| Kapadokya | 38.6431 | 34.8289 | Peri bacaları |
| İstanbul | 41.0082 | 28.9784 | Boğaz topografyası |

### 6.2 Performance Benchmark

```cpp
// Test metricleri
struct TerrainBenchmark {
    double avgFrameMs;
    double p95FrameMs;
    int demCacheHits;
    int demCacheMisses;
    double avgPickTimeUs;
    size_t gpuMemoryMB;
};
```

### 6.3 Visual Regression Test

```cpp
void GlobeEngine::RunTerrainTest() {
    // Önceden tanımlı lokasyonlara git
    // Screenshot al
    // Baseline ile karşılaştır
}
```

### Doğrulama Kriterleri
- [ ] Tüm test lokasyonlarında terrain görünüyor
- [ ] 60 FPS @ 1080p (orta seviye GPU)
- [ ] Memory leak yok (uzun süreli test)
- [ ] Tilt/orbit sırasında artifakt yok

---

## Implementasyon Sırası

```
Hafta 1:
├── FAZ 1: Parametre iyileştirmeleri (1 gün)
├── FAZ 2: GPU heightmap sistemi (3 gün)
└── Test & debug (1 gün)

Hafta 2:
├── FAZ 3: Vertex shader displacement (3 gün)
├── FAZ 4: Terrain picking (2 gün)
└── Test & debug (1 gün)

Hafta 3:
├── FAZ 5: Progressive loading (2 gün)
├── FAZ 6: Final test & polish (2 gün)
└── Dokümantasyon (1 gün)
```

---

## Risk Analizi

| Risk | Olasılık | Etki | Mitigasyon |
|------|----------|------|------------|
| DEM servisi yavaş/down | Orta | Yüksek | Cache agresif, fallback placeholder |
| GPU memory overflow | Düşük | Yüksek | LRU cache, texture compression |
| Shader compatibility | Düşük | Orta | GLSL 330 core (geniş destek) |
| Performans düşüşü | Orta | Orta | LOD-aware detail reduction |

---

## Bağımlılıklar

- OpenGL 3.3+ (R16F texture support)
- GLM (math operations)
- Mevcut DEM servisi (PiriReis)

---

## Notlar

- Bu plan AGENTS.md'deki "API/Behavior parity" kuralına uygundur
- Google Earth navigation parity korunacaktır
- Tüm değişiklikler mevcut API'yi bozmayacak şekilde yapılacaktır
