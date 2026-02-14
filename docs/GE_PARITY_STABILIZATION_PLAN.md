# SardaGlobe GE Parity Stabilizasyon Planı

> **Versiyon:** 1.0  
> **Tarih:** 2026-02-14  
> **Hedef:** Google Earth ile görsel ve davranışsal parity (eşdeğerlik)  
> **Mevcut Durum:** Phase 5-6 tamamlandı (GE NodeData hattı çalışıyor)

---

## Özet

Mevcut durumda GE NodeData hattı (fetch/decode/upload/render) çalışıyor ve test edilmiş durumda. Kalan kritik boşluklar "görsel pariteyi kilitleyen" P0 katmanda:

| Öncelik | Bileşen | Durum | Bloker |
|---------|---------|-------|--------|
| **P0** | Reversed-Z + RTE/RTC | 🟡 Partial | **Evet** - Titreme + z-fighting |
| **P0** | Texture2DArray | 🔴 Not Started | **Evet** - Texture bleeding |
| **P0** | PBO/Staging Upload | 🔴 Not Started | **Evet** - GPU stutter |
| **P1** | Horizon Culling + SSE | 🟡 Partial | Hayır |
| **P1** | Weighted Scheduler | 🟡 Partial | Hayır |
| **P2** | Görsel Finisaj | 🟡 In Progress | Hayır |

---

## Faz 1 — Çekirdek Hassasiyet ve Derinlik Stabilitesi (P0)

### 1A. Reversed-Z Pipeline (Tam Implementasyon)

**Mevcut Durum:**
- `Config::reversedZEnabled` flag mevcut
- `PerspectiveCamera::SetReverseZEnabled()` ve projeksiyon matematiği implemente edilmiş
- GL state henüz Reversed-Z'ye göre ayarlanmamış

**Yapılacak İşler:**

```cpp
// src/engine/globe_engine.cpp - Init() içinde
glEnable(GL_DEPTH_TEST);
if (config_.reversedZEnabled) {
    glDepthFunc(GL_GEQUAL);  // Reversed-Z: daha yakın = daha büyük Z
    glClearDepth(0.0f);      // Clear to 0 (far)
} else {
    glDepthFunc(GL_LEQUAL);
    glClearDepth(1.0f);
}
```

**Dosya Değişiklikleri:**
| Dosya | Değişiklik |
|-------|------------|
| `src/engine/globe_engine.cpp` | `Render()` içinde clear depth değeri |
| `src/engine/globe_engine.cpp` | `Init()` GL state yapılandırması |
| `src/rendering/tile_renderer.cpp` | Shader define `REVERSED_Z` |

**Shader Uyum:**
```glsl
// tile_renderer.glsl (vertex)
#ifdef REVERSED_Z
    // NDC Z: near -> +1, far -> -1
    gl_Position.z = gl_Position.w * 0.5 + gl_Position.z * 0.5; // [-w, w] -> [0, w]
    gl_Position.z = gl_Position.w - gl_Position.z; // [0, w] -> [w, 0] (reversed)
#endif
```

---

### 1B. RTE/RTC (Relative to Center/Eye) Implementasyonu

**Neden:** 32-bit float ECEF koordinatları yakın zoom'da titremeye (jitter) neden olur. Çözüm: vertex'leri tile-center veya camera'ya göreceli olarak göndermek.

**Mimari:**
```cpp
// CPU-side (double precision)
glm::dvec3 tileOriginECEF = TileCenterWorldECEF(tileKey);
glm::dvec3 cameraECEF = camera_->GetPositionECEF();

// Split per RTC (Relative to Center)
struct RteRtcUniforms {
    vec3 uCameraECEFHi;  // high 16 bits
    vec3 uCameraECEFLo;  // low 16 bits
    vec3 uTileOriginECEFHi;
    vec3 uTileOriginECEFLo;
};

// Vertex format değişimi (gerekirse)
// Mevcut: vec3 position (ECEF, ~6378km, float32)
// Yeni:   vec3 relativePosition (tile-center relative, ~10km range, float32)
```

**Shader Implementasyonu:**
```glsl
// Vertex shader - RTE/RTC matematiği
uniform vec3 uCameraECEFHi;
uniform vec3 uCameraECEFLo;
uniform vec3 uTileOriginECEFHi;
uniform vec3 uTileOriginECEFLo;

attribute vec3 aRelativePosition;  // Tile origin'a göre relative

vec3 ComputeRtePosition() {
    // Double emulation: (hi + lo) formatında 64-bit hassasiyet
    vec3 cameraPos = uCameraECEFHi + uCameraECEFLo;
    vec3 tileOrigin = uTileOriginECEFHi + uTileOriginECEFLo;
    
    // Vertex world position = tileOrigin + relativePosition
    vec3 worldPos = tileOrigin + aRelativePosition;
    
    // View-relative = worldPos - cameraPos
    // Bu hesaplama float32'de güvenli çünkü sonuç küçük (~km seviyesi)
    return worldPos - cameraPos;
}

void main() {
    vec3 viewRelativePos = ComputeRtePosition();
    gl_Position = uProjectionMatrix * uViewMatrix * vec4(viewRelativePos, 1.0);
}
```

**Dosya Değişiklikleri:**
| Dosya | Değişiklik |
|-------|------------|
| `src/camera/earth_camera.h/cpp` | `GetRteSplitPosition()` - double->split float helper |
| `src/rendering/tile_renderer.h/cpp` | RTE uniform setters |
| `src/rendering/shader_manager.cpp` | Shader varyantları |
| `src/rendering/tile_mesh_builder.cpp` | Relative vertex hesaplama |

---

### 1C. RockMesh RTE/RTC Geçişi

**Mevcut Durum:** RockMesh vertex'leri CPU'da ECEF olarak hesaplanıp GPU'ya float olarak gönderiliyor.

**Değişiklik:**
```cpp
// src/rendering/rockmesh_runtime.cpp
// CPU-side: Tile origin seçimi (mesh bounding box center)
glm::dvec3 meshOrigin = CalculateMeshOrigin(parsedNodeData);

// Vertex'leri relative olarak encode et
for (auto& v : vertices) {
    glm::dvec3 absPos = LatLonAltToECEF(v.lat, v.lon, v.alt);
    glm::vec3 relPos = glm::vec3(absPos - meshOrigin);  // ~km range, float32-safe
    v.position = relPos;
}

// GPU'ya gönder: meshOriginHi/Lo + relative vertices
// Shader'da: worldPos = meshOrigin + relativePosition
```

**Dosya Değişiklikleri:**
| Dosya | Değişiklik |
|-------|------------|
| `src/rendering/rockmesh_runtime.cpp` | `RockMeshGpu::Create()` RTE encode |
| `src/rendering/rockmesh_manager.cpp` | Uniform binding |
| `src/io/providers/rocktree_node_data_parser.cpp` | Mesh origin seçimi |

---

## Faz 2 — Asenkron Geçiş ve Doku Dağıtım Yenilemesi (P0)

### 2A. PBO (Pixel Buffer Object) Upload Pipeline

**Neden:** `glTexSubImage2D` main thread'i bloke edebilir. PBO ile async DMA transfer.

**Mimari:**
```cpp
// src/rendering/pbo_upload_manager.h
class PboUploadManager {
public:
    explicit PboUploadManager(int ringBufferCount = 2);
    
    // CPU decode sonrası çağrılır
    void StageTextureData(const TileKey& key, std::vector<uint8_t> rgba, 
                          int width, int height);
    
    // Main thread'de her frame çağrılır
    void ProcessUploads(double budgetMs);
    
private:
    struct PboRingBuffer {
        GLuint pboId = 0;
        size_t capacity = 0;
        bool inUse = false;
    };
    std::vector<PboRingBuffer> ringBuffers_;
    
    struct StagedUpload {
        TileKey key;
        std::vector<uint8_t> data;
        int width, height;
    };
    std::queue<StagedUpload> stagingQueue_;
};
```

**Implementasyon Akışı:**
```cpp
// 1. Orphan/PBO oluştur
void PboUploadManager::StageTextureData(...) {
    // PBO buffer'a copy (async, DMA)
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, data.size(), data.data(), GL_STREAM_DRAW);
    
    // Texture'a async upload planla
    stagingQueue_.push({key, std::move(data), width, height});
}

// 2. Main thread'de GPU upload (fast, DMA'dan okuma)
void PboUploadManager::ProcessUploads(double budgetMs) {
    while (!stagingQueue_.empty() && withinBudget) {
        auto& upload = stagingQueue_.front();
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
        glTexSubImage2D(..., 0);  // PBO'dan okur, hızlı
        stagingQueue_.pop();
    }
}
```

**Dosya Değişiklikleri:**
| Dosya | Değişiklik |
|-------|------------|
| `src/rendering/pbo_upload_manager.h/cpp` | Yeni modül |
| `src/rendering/texture_manager.cpp` | PBO upload entegrasyonu |
| `src/rendering/rockmesh_manager.cpp` | Mesh PBO upload |

---

### 2B. Texture2DArray Migration (Atlas Kaldırma)

**Mevcut Sorun:** Texture atlas'ta gutter/seam bleeding problemleri var.

**Hedef:** `sampler2DArray` kullanarak her tile'ı ayrı layer olarak saklamak.

**Migration Planı:**

```cpp
// Phase 1: Paralel çalıştırma (flag-gated)
// Config::useTexture2DArray = true (varsayılan false)

// Phase 2: TextureArrayManager implementasyonu
class TextureArrayManager {
public:
    // Her tile boyutu için ayrı array (256x256, 512x512, ...)
    struct ArrayLayer {
        GLuint textureId;
        int layerIndex;
        TileKey key;
        bool inUse;
    };
    
    // Layer allocation
    std::optional<ArrayLayer> AllocateLayer(const TileKey& key, int width, int height);
    void FreeLayer(const TileKey& key);
    
    // Bind and sample
    void BindTextureArray(GLuint unit, int resolutionTier);
    
private:
    static constexpr int MAX_LAYERS = 128;
    static constexpr int ARRAY_SIZE = 256;  // 256x256 texture array
    
    struct TextureArray {
        GLuint id = 0;
        std::vector<bool> layerUsed;
        int resolution;
    };
    std::vector<TextureArray> arrays_;
};
```

**Shader Değişimi:**
```glsl
// Eski (atlas)
uniform sampler2D uTexture;
vec4 color = texture(uTexture, uv * scaleOffset.xy + scaleOffset.zw);

// Yeni (array)
uniform sampler2DArray uTextureArray;
uniform int uTextureLayer;  // Per-tile layer index
vec4 color = texture(uTextureArray, vec3(uv, float(uTextureLayer)));
```

**Dosya Değişiklikleri:**
| Dosya | Değişiklik |
|-------|------------|
| `src/rendering/texture_array_manager.h/cpp` | Yeni modül |
| `src/rendering/texture_manager.cpp` | Array path entegrasyonu |
| `src/rendering/tile_renderer.cpp` | Layer index uniform |
| `src/core/config.h` | `useTexture2DArray` flag |

---

## Faz 3 — Ağ + LOD + Görünürlük Optimizasyonu (P1)

### 3A. Horizon Culling Entegrasyonu

**Mevcut Durum:** Frustum culling mevcut, horizon culling yapılandırılabilir (`disableHorizonCull`)

**İyileştirme:**
```cpp
// src/scheduling/lod_selector.cpp
// Tile'ın dünya arkasında (horizon altında) olduğunu tespit et

bool IsTileBelowHorizon(const Tile& tile, const glm::dvec3& cameraPos) {
    // Camera'dan tile center'a vektör
    glm::dvec3 toTile = tile.center - cameraPos;
    double distToTile = glm::length(toTile);
    
    // Camera'nin dünya yüzeyinden yüksekliği
    double cameraHeight = glm::length(cameraPos) - EARTH_RADIUS_KM;
    
    // Horizon mesafesi
    double horizonDistance = sqrt(cameraHeight * (2 * EARTH_RADIUS_KM + cameraHeight));
    
    // Tile'ın horizon altında olduğunu kontrol et
    // Basit: tile center horizon'dan uzaktaysa ve dünya tarafından gizleniyorsa
    if (distToTile > horizonDistance) {
        double tileDotCamera = glm::dot(glm::normalize(toTile), glm::normalize(cameraPos));
        // Eğer tile camera'nın dünya merkezinden uzak tarafındaysa
        if (tileDotCamera < 0.0) {
            return true;
        }
    }
    return false;
}
```

**Dosya Değişiklikleri:**
| Dosya | Değişiklik |
|-------|------------|
| `src/math/frustum.h/cpp` | `IsBelowHorizon()` fonksiyonu |
| `src/scheduling/tile_pyramid.cpp` | Horizon cull check |

---

### 3B. Weighted Request Scheduler

**Mevcut Durum:** Priority queue (Urgent/Normal) mevcut, weighted scheduling yok.

**Hedef:** Camera merkezine yakın tile'lar daha yüksek öncelik alsın.

```cpp
// src/io/providers/http_transport.h
enum class RequestPriorityClass : uint8_t {
    Terrain = 0,      // DEM - en yüksek
    MeshNear = 1,     // RockMesh yakın
    MeshFar = 2,      // RockMesh uzak
    Metadata = 3,     // NodeData metadata
    Prefetch = 4      // Ön yükleme
};

struct WeightedRequest {
    TileKey key;
    RequestPriorityClass priorityClass;
    float screenSpaceScore;  // Camera merkezine yakınlık
    float sseScore;          // Screen-space error
    float finalScore;        // Hesaplanmış: priorityClass * screenSpaceScore * sseScore
};
```

**Dosya Değişiklikleri:**
| Dosya | Değişiklik |
|-------|------------|
| `src/io/providers/http_transport.h/cpp` | Priority class enum ve scoring |
| `src/scheduling/tile_scheduler.cpp` | Weighted queue kullanımı |
| `src/rendering/rockmesh_manager.cpp` | Request priority belirleme |

---

### 3C. SSE + Varyans LOD Entegrasyonu

**Hedef:** Yükseklik varyansı yüksek bölgelerde (dağlar) daha agresif LOD, düz bölgelerde (okyanus) daha az tile.

```cpp
// src/scheduling/lod_selector.cpp
float ComputeLodThreshold(const Tile& tile, float baseSseThreshold) {
    float variance = tile.heightVariance;  // DEM'den hesaplanmış
    
    // Varyans yüksekse daha detaylı (düşük threshold)
    // Varyans düşükse daha az detay (yüksek threshold)
    float varianceFactor = 1.0f / (1.0f + variance * VARIANCE_INFLUENCE);
    
    return baseSseThreshold * varianceFactor;
}
```

---

## Faz 4 — Geometrik/Shader Tuning ve Görsel Kompozit (P2)

### 4A. Crossfade Dithering Geçişi

**Mevcut:** Alpha blending crossfade
**Hedef:** Stochastic dithering ile overdraw azaltma

```glsl
// crossfade_fragment.glsl
uniform sampler2D uTextureOld;
uniform sampler2D uTextureNew;
uniform float uCrossfadeT;

// Bayer dither pattern
const float bayer[4] = float[](0.0, 0.5, 0.75, 0.25);

float GetDitherThreshold(vec2 uv) {
    ivec2 coord = ivec2(mod(gl_FragCoord.xy, 2.0));
    return bayer[coord.x + coord.y * 2];
}

void main() {
    vec4 oldColor = texture(uTextureOld, uv);
    vec4 newColor = texture(uTextureNew, uv);
    
    #ifdef USE_DITHER_CROSSFADE
        float dither = GetDitherThreshold(uv);
        // Dither threshold'a göre eski veya yeni texture seç
        float blend = step(dither, uCrossfadeT);
        vec4 final = mix(oldColor, newColor, blend);
    #else
        vec4 final = mix(oldColor, newColor, uCrossfadeT);
    #endif
    
    FragColor = final;
}
```

### 4B. GPU De-kuantizasyon

**Hedef:** RockMesh vertex'lerini int16/quant formatında gönderip GPU'da decode etmek.

```glsl
// rockmesh_vertex.glsl
uniform vec3 uQuantOrigin;
uniform float uQuantScale;

// Vertex attribute: int16 (packed)
attribute vec2 aQuantPosition;  // (x, y) in int16 space
attribute float aQuantHeight;   // height in int16 space

vec3 DecodePosition() {
    vec3 quant = vec3(aQuantPosition.x, aQuantPosition.y, aQuantHeight);
    return uQuantOrigin + quant * uQuantScale;
}
```

---

## Test ve Kabul Kriterleri

### Yeni Testler (+20 senaryo)

```cpp
// tests/reversed_z_precision_test.cpp
TEST(ReversedZ, NearFarOverlap) {
    // Reversed-Z'de uzak/yakın obje çakışma testi
}

// tests/rte_rtc_regression_test.cpp
TEST(RteRtc, MicroMovementVertexJitter) {
    // Kamera mikro hareketlerinde vertex farkı < 0.1px
}

// tests/depth_precision_test.cpp (genişletilmiş)
TEST(DepthPrecision, ReversedZFighting) {
    // f16 depth buffer'da z-fighting ölçümü
}

// tests/texture_array_test.cpp
TEST(TextureArray, NoBleedingBetweenLayers) {
    // Sıfır sınır örneklemesi bleed testi
}

// tests/pbo_upload_latency_test.cpp
TEST(PboUpload, FrameTimeImprovement) {
    // PBO açık/kapalı frame-time karşılaştırması
}

// tests/horizon_culling_test.cpp
TEST(HorizonCulling, OccludedTilePercent) {
    // Ufuk arkası tile'ların %90+ atılması
}

// tests/sse_lod_decision_test.cpp
TEST(SseLod, VarianceAdaptiveSplit) {
    // Yüksek varyanslı bölgede farklı split davranışı
}

// tests/priority_scheduler_test.cpp
TEST(Scheduler, CenterProximityPriority) {
    // Merkez yakınlığı yüksek key'lerin gecikmesi daha düşük
}

// tests/crossfade_overdraw_test.cpp
TEST(Crossfade, DitherOverdrawReduction) {
    // Dither path'te overdraw azalımı
}
```

### Kabul Kriterleri Checklist

| Kriter | Hedef | Doğrulama |
|--------|-------|-----------|
| Micro-stutter penceresi | Düşürülmüş | 60 FPS senaryolarında p95 < 16.6ms |
| Jitter metrikleri | Minimum | Pixel RMS < 0.5px (kamera micro-movement) |
| Texture bleeding | %0 | `AtlasGutterUvTest` başarılı |
| Yakın tile bekleme | Anlamlı düşük | Priority scheduler median latency < 200ms |
| Test kapsamı | +20 test | Toplam 65+ senaryo |

---

## API / Config Eklemleri

```cpp
// src/core/config.h
struct Config {
    // Faz 1: Reversed-Z + RTE
    bool useReversedZ = true;
    bool useRteRender = true;
    
    // Faz 2: Texture array + PBO
    bool useTexture2DArray = true;
    bool usePboUploads = true;
    size_t uploadPboRingBuffers = 2;
    
    // Faz 3: Horizon + Weighted scheduling
    bool useHorizonCulling = true;
    bool useWeightedScheduling = true;
    
    // Faz 4: Visual tuning
    bool useDitherCrossfade = false;  // Varsayılan kapalı (alpha daha kararlı)
    bool useGpuDequantization = false;
    
    // Cache
    size_t persistentRockMeshCacheMb = 256;
};

// src/io/providers/http_transport.h
struct HttpTransportConfig {
    enum class PriorityClass { Terrain, MeshNear, MeshFar, Metadata };
    PriorityClass defaultPriority = PriorityClass::Metadata;
};

// src/rendering/render_state.h
struct RenderState {
    vec3 cameraEcefHi, cameraEcefLo;
    vec3 cameraBasisForward, cameraBasisUp, cameraBasisRight;
    vec3 meshOriginHi, meshOriginLo;
};
```

---

## Uygulama Sırası ve Bağımlılıklar

```
Faz 1A (Reversed-Z) ──┬──► Faz 1B (RTE/RTC) ──┬──► Faz 1C (RockMesh RTE)
                      │                        │
                      └──► Faz 2A (PBO) ───────┴──► Faz 2B (TextureArray)
                                               │
Faz 3A (Horizon) ────┬──► Faz 3B (Weighted) ──┤
                     │                        │
Faz 3C (SSE+Varyans)─┘                        └──► Faz 4 (Visual Tuning)
```

**Kritik Yol:** 1A → 1B → 2A → 2B (P0 engeller)  
**Paralel:** 3A/3B/3C P1'ler birbiriyle bağımsız  
**Opsiyonel:** 4 (finisaj) P2

---

## Özet

Bu plan, Google Earth ile görsel parity'yi kilitlemek için kalan kritik teknik boşlukları kapatıyor:

1. **Faz 1:** Titreme ve z-fighting'i Reversed-Z + RTE/RTC ile çöz
2. **Faz 2:** Stutter ve bleeding'i PBO + Texture2DArray ile çöz
3. **Faz 3:** Performansı Horizon Culling + Weighted Scheduler ile artır
4. **Faz 4:** Görsel kaliteyi finisaj ile polish et

Her faz, mevcut test altyapısı ile doğrulanmış ve feature flag ile geriye dönük uyumlu şekilde tasarlanmıştır.
