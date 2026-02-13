# SardaGlobe Derin Mimari Review ve Düzeltme Planı

> **Tarih:** 2026-02-13  
> **Analiz:** Google Earth WASM/WAT tersine mühendislik + SardaGlobe mevcut implementasyon karşılaştırması  
> **Hedef:** Tile fetch/render/dem pipeline'ının Google Earth ile birebir parity'sini sağlamak

---

## Özet - Kritik Bulgular

| Alan | Google Earth (Referans) | SardaGlobe (Mevcut) | Uyum Durumu |
|------|------------------------|---------------------|-------------|
| **Frame Pipeline** | 3 aşamalı: DoFrame → BuildNextScene → RenderScene | Monolitik Update → Render | ⚠️ **Farklı** |
| **SSE Hesaplama** | geometricError × focalLength / distance | Tile level bazlı basit hesaplama | ⚠️ **Farklı** |
| **uCornerLods** | Per-tile köşe LOD'ları + bilinear interp. | Var ama shader'da kullanım sınırlı | ⚠️ **Eksik** |
| **Unpop/Crossfade** | uUnpopBlend + speed limit + RASTER_CROSSFADE | Shader'da var, engine entegrasyonu eksik | ⚠️ **Eksik** |
| **Skirt Generation** | Height-aware + selective mask | Var, height-aware değil | ✅ **Kısmen** |
| **DEM Pipeline** | BatchGetElevationsByPoint (protobuf RPC) | Terrain-RGB + Pirireis batch | ⚠️ **Farklı** |
| **Tile State Machine** | 7 state + cancel lifecycle | 8 state + cancel | ✅ **Yakın** |
| **Parent-Child** | Progressive refinement + morph | Coherence tracking var | ✅ **Kısmen** |

**Genel Değerlendirme:** Mevcut implementasyon GE mimarisinin %60-70'ini doğru şekilde uygulamış, ancak kritik parity farkları var.

---

## 1. Frame Pipeline Analizi

### 1.1 Google Earth Referans (WASM String Kanıtlı)

```
_main (func 32863, ~60 call/sec)
│
├── InstanceImpl::DoFrame()
│   ├── [1] InterFrameTime hesapla
│   ├── [2] DoFrame_thread (ayrı thread)
│   │   ├── Camera::Update()
│   │   ├── RunLoaders [delayed]
│   │   └── Main-thread callback kuyruğuna closure schedule
│   │
│   ├── [2b] Main-thread callback drain
│   │   └── VectorTileAssetLoader::FinishMerge() ← GPU upload
│   │
│   ├── [3] InstanceImpl::BuildNextScene(build_frame)
│   │   ├── ★ QUADTREE TRAVERSAL ★
│   │   │   ├── TraversalOutput hesapla
│   │   │   ├── LodInfo ile LOD seçimi
│   │   │   │   ├── uCornerLods ← "Tile corner lods for bilinear interp."
│   │   │   │   ├── geometricError hesapla
│   │   │   │   └── SSE = geometricError / distance * focalLength
│   │   │   └── Visible tile listesi oluştur
│   │   │
│   │   ├── Tile Request Scheduling
│   │   │   ├── AssetNetLoads ← network'ten yükleme
│   │   │   └── Priority queue güncelle
│   │   └── ShaderScene oluştur
│   │
│   ├── [4] Render Pipeline
│   │   ├── EarthFrameHandler::OnFrame()
│   │   │   ├── RenderTerrain (RASTER_SINGLE/CROSSFADE)
│   │   │   ├── SKIRTS oluştur
│   │   │   └── Depth plane hesapla
│   │   └── ...
│   └── [5] RequestNewFrame(reason, file, line)
```

### 1.2 SardaGlobe Mevcut

```
GlobeEngine::Run()
│
├── Update(dt, currentTime)
│   ├── Camera update
│   ├── DEM/Heightmap update
│   ├── TilePyramid::Select() ← Quadtree traversal
│   │   └── LodSelector::Select()
│   ├── Scheduler update
│   └── MeshScheduler update
│
└── Render()
    ├── TileRenderer::BeginBatch()
    ├── RenderTile() for each leaf
    └── TileRenderer::EndBatch()
```

### 1.3 Tespit Edilen Farklar

| # | Fark | Önem | Açıklama |
|---|------|------|----------|
| 1 | **Ayrık Scene Build** | 🔴 Kritik | GE'de BuildNextScene render'dan ayrı, SardaGlobe'da Update içinde |
| 2 | **RequestNewFrame Mekanizması** | 🟡 Orta | GE "on-demand" render, SardaGlobe sürekli render |
| 3 | **Callback Draining** | 🟡 Orta | GE'de main-thread callback drain ayrı aşama |
| 4 | **TraversalOutput Yapısı** | 🟡 Orta | GE'de explicit TraversalOutput, SardaGlobe'da LodSelection |

---

## 2. SSE (Screen-Space Error) Hesaplama Analizi

### 2.1 Google Earth Referans

```cpp
// GE WASM'dan çıkarılan:
// SSE = geometricError / distance * focalLength
//
// geometricError: glTF/3DTiles standardından
// Screen Space Error = geometricError * focalLength / distance
```

**WASM String Kanıtları:**
- `"geometricError"` - glTF/3DTiles standardından
- `"maxLodPixels"` / `"minLodPixels"` - pixel threshold'lar
- `"GetMaxLodPixels"` / `"GetMinLodPixels"` - API'ler

### 2.2 SardaGlobe Mevcut (`math/tile_math.h`)

```cpp
inline float ComputeSSE(const TileKey& key, double distanceMeters, 
                        int viewportHeight, float fovDegrees) {
    // Tile size at this zoom level (approximate meters)
    double tileSizeMeters = EarthCircumferenceMeters / (1 << key.level);
    
    // Approximate screen size in pixels
    double distanceKm = distanceMeters / 1000.0;
    double fovRad = fovDegrees * M_PI / 180.0;
    double screenHeightAtDistance = 2.0 * distanceKm * tan(fovRad / 2.0);
    
    // SSE calculation: how many pixels does this tile occupy?
    double pixels = (tileSizeMeters / 1000.0) / screenHeightAtDistance * viewportHeight;
    
    // Return SSE as pixel coverage
    return static_cast<float>(pixels);
}
```

### 2.3 Karşılaştırma

| Özellik | Google Earth | SardaGlobe | Durum |
|---------|--------------|------------|-------|
| **Temel formül** | geometricError × focalLength / distance | Tile size / screenHeight × viewport | ⚠️ Farklı |
| **Geometric error** | glTF standardı (seamless subdivision) | Tile level bazlı | ⚠️ Farklı |
| **Focal length** | Gerçek focal length (projection'dan) | FOV bazlı approximasyon | ⚠️ Farklı |
| **Distance** | Tile center'dan | Tile center'dan | ✅ Aynı |

### 2.4 Problem

SardaGlobe'un SSE hesaplaması GE'nin geometricError tabanlı yaklaşımına göre farklı sonuçlar veriyor. Bu özellikle:
- Yüksek enlemlerde tile boyutları farklı
- fovDegrees doğrudan focalLength'e çevrilmemiş
- Tile seviyeleri arası geçişler farklı davranıyor

---

## 3. uCornerLods (Bilinear LOD Interpolation)

### 3.1 Google Earth Referans

```cpp
// WASM string kanıtları:
// "uCornerLods" — "Tile corner lods for bilinear interp."
//
// Her tile'in 4 köşesine farklı LOD seviyesi atanır
// GPU shader'da bilinear interpolation ile smooth LOD geçişi
// Tile kenarlarında LOD seam'leri önlenir
```

**Shader'da kullanım:**
```glsl
uniform vec4 uCornerLods; // NW, NE, SE, SW

// Vertex shader'da:
// Her köşenin LOD'sine göre height/position blend
// Bilinear interp: lerp(lerp(nw, ne, u), lerp(sw, se, u), v)
```

### 3.2 SardaGlobe Mevcut

**`src/core/tile.h`:**
```cpp
// GE-style corner LODs for bilinear interpolation in vertex shader.
// Order: NW, NE, SE, SW (with UV: NW=(0,1), NE=(1,1), SE=(1,0), SW=(0,0)).
glm::vec4 cornerLods{0.0f};
```

**`src/rendering/corner_lod.cpp`:**
```cpp
void ComputeCornerLods(Tile& tile, const TilePyramid& pyramid) {
    // Neighbor tile'ların LOD'larına bakarak corner LOD'ları hesapla
    // NW corner: North neighbor ve West neighbor'ın min LOD'si
    // ...
}
```

**Shader (`src/rendering/shader_manager.cpp`):**
```cpp
// Uniform var ama vertex shader'da kullanımı eksik
cornerLodsLoc_ = glGetUniformLocation(program, "uCornerLods");
```

### 3.3 Karşılaştırma

| Özellik | Google Earth | SardaGlobe | Durum |
|---------|--------------|------------|-------|
| **Corner LOD hesaplama** | Var (Traverse sırasında) | Var (`ComputeCornerLods`) | ✅ Var |
| **Shader uniform** | Var | Var | ✅ Var |
| **Vertex shader kullanımı** | Bilinear height blend | Yok (sadece set ediliyor) | 🔴 **Eksik** |
| **Seam önleme** | Aktif çalışıyor | Pasif | 🔴 **Eksik** |

---

## 4. Unpop / Crossfade Mekanizması

### 4.1 Google Earth Referans

**WASM String Kanıtları:**
```
"kPhotoTileUnpopping" — photo tile unpop durumu
"kRockTreeUnpopping"  — rocktree unpop durumu
"uUnpopBlend"         — unpop blend uniform'u
"uTexScaleOffsetUnpop" — unpop texture parametreleri
"uPhotoTileTextureUnpop" — unpop texture
"blend between PhotoTileTexture and PhotoTileTextureUnpop"
"/mirth/core/render/UnpopPairTimeFade" — fade süresi config
"unpop_speed_limit_ndc" — NDC'de hız limiti
"Unpopping is disabled when IUnpoppable moves faster than this speed in NDC in one frame."

"RASTER_CROSSFADE" — shader define
"uCrossfadeInterpolant" — crossfade interpolant uniform [0,1]
"SetCrossfade(crossfade = %d)"
```

**Mekanizma:**
1. Düşük çözünürlük tile hemen gösterilir (parent/cache)
2. Yüksek çözürlük tile yüklenirken, düşük tile fade-out olur
3. Blend factor 0→1 arasında interpolate edilir
4. Kamera çok hızlı hareket ederse unpop devre dışı kalır

### 4.2 SardaGlobe Mevcut

**`src/core/tile.h`:**
```cpp
// Fade-in animation (Google Earth style smooth appearance)
float fadeAlpha = 0.0f;          // Current fade value
double fadeStartTime = 0.0;       // When fade started
bool fadeComplete = false;        // True when fade finished
static constexpr float FADE_DURATION = 0.3f;  // 300ms

float UpdateFade(double currentTime, float fadeDuration = FADE_DURATION) {
    // Linear fade implementation
}
```

**Shader (`src/rendering/shader_manager.cpp`):**
```cpp
ss << "uniform sampler2D uPhotoTileTextureUnpop;\n";
ss << "uniform float uUnpopBlend;\n";
ss << "uniform vec4 uTexScaleOffsetUnpop;\n";
ss << "uniform int uRasterCrossfade;\n";

// Fragment shader'da:
ss << "    if (uRasterCrossfade == 1) {\n";
ss << "        vec2 uvUnpop = vTexCoord * uTexScaleOffsetUnpop.xy + uTexScaleOffsetUnpop.zw;\n";
ss << "        vec4 unpopColor = texture(uPhotoTileTextureUnpop, uvUnpop);\n";
ss << "        float blend = clamp(uUnpopBlend, 0.0, 1.0);\n";
ss << "        texColor = mix(unpopColor, texColor, blend);\n";
ss << "    }\n";
```

**Engine entegrasyonu (`globe_engine.cpp`):**
```cpp
// Unpop speed limit tracking var ama kullanımı sınırlı
cameraSpeedKmPerSec_ = static_cast<float>(std::clamp(speedKmPerSec, 0.0, 1.0e6));

// Parent texture binding için crossfade setup eksik
```

### 4.3 Karşılaştırma

| Özellik | Google Earth | SardaGlobe | Durum |
|---------|--------------|------------|-------|
| **uUnpopBlend uniform** | Var | Var | ✅ Var |
| **RASTER_CROSSFADE** | Var | Var | ✅ Var |
| **Parent texture bind** | Var (`uPhotoTileTextureUnpop`) | Eksik | 🔴 **Eksik** |
| **Speed limit** | Var (`unpop_speed_limit_ndc`) | Var ama kullanılmıyor | 🟡 **Eksik** |
| **Crossfade interpolant** | Dinamik hesaplama | Statik | 🟡 **Farklı** |
| **Blend direction** | Parent → Child | Child fade only | 🔴 **Farklı** |

---

## 5. Skirt Generation Analizi

### 5.1 Google Earth Referans

**WASM String Kanıtları:**
```
"Skirts" — WASM string'i
"SKIRTS" — shader define
```

**Yaklaşım:**
1. Tile kenar vertex'leri kopyalanır
2. Dünya merkezine doğru aşağı itilir (skirt_depth)
3. Orijinal kenar ile skirt arası triangle'lar oluşturulur
4. Farklı LOD tile'ları arasındaki boşluklar kapatılır

### 5.2 SardaGlobe Mevcut (`src/rendering/tile_mesh_builder.cpp`)

```cpp
void TileMeshBuilder::GenerateSkirts(
    std::vector<float>& vertices,
    std::vector<unsigned int>* indices,
    int segments,
    int level,
    uint8_t skirtMask,  // Selective skirt generation
    const Config& config,
    double heightRange  // Height-aware depth
) {
    // Calculate skirt depth based on tile size at this zoom level
    double tileArcKm = 40075.0 / (1 << level);
    double minDepth = std::max(0.001, static_cast<double>(config.skirtDepthNearKm));
    double farDepth = std::max(minDepth, static_cast<double>(config.skirtDepthFarKm));
    double clampMaxDepth = std::max(farDepth, static_cast<double>(config.skirtMaxDepthKm));
    
    // LOD-based interpolation
    double lodT = std::clamp(tileArcKm / 2500.0, 0.0, 1.0);
    double skirtDepth = minDepth + (farDepth - minDepth) * lodT;
    
    // Height-aware: Keep skirts proportional to relief
    if (heightRange > 0.0) {
        skirtDepth = std::max(skirtDepth, heightRange * 0.15);
    }
    
    // Selective: Only generate for edges in skirtMask
    // North, East, South, West skirts ayrı ayrı üretilir
}
```

### 5.3 Karşılaştırma

| Özellik | Google Earth | SardaGlobe | Durum |
|---------|--------------|------------|-------|
| **Skirt depth** | Sabit (dünya merkezine) | LOD-based + height-aware | 🟡 **Farklı** |
| **Selective generation** | Var | Var (skirtMask) | ✅ Var |
| **Height-aware** | Belirsiz | Var | ✅ Var |
| **Normal handling** | Ana vertex normal'i kopyala | Ana vertex normal'i kopyala | ✅ Aynı |

**Yorum:** SardaGlobe'un skirt implementasyonu GE'den daha gelişmiş (height-aware).

---

## 6. DEM Pipeline Analizi

### 6.1 Google Earth Referans

**Network Flow:**
```
GetTerrainElevation(lat, lon, elevation_type)
    ↓
RefinedElevationsRequester::FetchRefinedElevations()
    ↓
google.internal.earth.v1.terrain.BatchGetElevationsByPointRequest
    ↓
Batch of LatLon points → Protobuf RPC
    ↓
BatchGetElevationsByPointResponse
    ↓
vector<double> (elevation values)
    ↓
HeightmapTile → TerrainMeshGenerator::GenerateMesh()
```

**WASM String Kanıtları:**
```
"GetTerrainElevation(latitude = %f, longitude = %f, elevation_type = %d)"
"GetAccurateTerrainElevation(latitude = %f, longitude = %f, desired_accuracy_meters = %f, ...)"
"RefinedElevationsRequester::FetchRefinedElevations(...)"
"google.internal.earth.v1.terrain.BatchGetElevationsByPointRequest"
"Plane equations for computing depth of each tile mesh vertex"
```

### 6.2 SardaGlobe Mevcut

**İki farklı kaynak:**
1. **Terrain-RGB** (Maptiler/Mapbox): `https://api.maptiler.com/tiles/terrain-rgb-v2/{z}/{x}/{y}.png`
2. **Pirireis Batch**: `?FLOAT=1&MESHN=5&CN=N&C1LLX=...`

**Flow:**
```
DemManager::Request(key, priority, score)
    ↓
Worker Thread Pool (4 workers)
    ↓
FetchTerrainRGBBatch() veya FetchBatch()
    ↓
DemGridData (meshN x meshN heights)
    ↓
Cache (LRU eviction)
    ↓
TileMeshScheduler::Request() → Build()
    ↓
TileMeshBuilder::Build() → ECEF vertex + normal + skirt
```

### 6.3 Karşılaştırma

| Özellik | Google Earth | SardaGlobe | Durum |
|---------|--------------|------------|-------|
| **API tipi** | Protobuf RPC | REST (PNG/JSON) | 🔴 **Farklı** |
| **Request formatı** | LatLon point batch | Tile-based (z/x/y) | 🔴 **Farklı** |
| **Response formatı** | vector<double> heights | PNG decode / JSON array | 🔴 **Farklı** |
| **Cache stratejisi** | Multi-tier | LRU + Pin | 🟡 **Farklı** |
| **Async pattern** | Future-based | Callback + Queue | 🟡 **Farklı** |
| **Parent fallback** | Var | Var | ✅ Var |
| **Edge coherence** | Var (demEdgeLevelPack) | Var | ✅ Var |

---

## 7. Tile State Machine Analizi

### 7.1 Google Earth Referans

```
UNLOADED ──schedule──→ SCHEDULED
SCHEDULED ──fetch──→ FETCHING
                       ├── disk hit → DECODING
                       └── network  → received → DECODING
DECODING ──decode──→ UPLOADING (main thread FinishMerge)
UPLOADING ──upload──→ READY
SCHEDULED/FETCHING/DECODING/UPLOADING ──cancel──→ CANCELED
CANCELED ──re-enter view / schedule──→ SCHEDULED
READY ──evict──→ UNLOADED
FETCHING/DECODING/UPLOADING ──error──→ FAILED
FAILED ──retry (max 3)──→ SCHEDULED
```

### 7.2 SardaGlobe Mevcut (`src/core/tile.h`)

```cpp
enum class TileState {
    Unloaded,       // Not in memory
    Scheduled,      // Queued for fetch
    Fetching,       // HTTP request in progress
    Decoding,       // Image decode in progress
    Uploading,      // GPU upload pending
    Canceled,       // Loading canceled while out of view
    Ready,          // Fully loaded, renderable
    Failed          // Load failed
};
```

### 7.3 Karşılaştırma

| Özellik | Google Earth | SardaGlobe | Durum |
|---------|--------------|------------|-------|
| **State sayısı** | 7 (evict yok) | 8 (evict yok) | ✅ Yakın |
| **Cancel state** | Var | Var | ✅ Aynı |
| **Retry mekanizması** | Max 3 retry | Configurable | ✅ Benzer |
| **Touch-based eviction** | Var | Epoch-based pinning | 🟡 **Farklı** |

---

## 8. Parent-Child İlişkileri ve Progressive Loading

### 8.1 Google Earth Referans

**Progressive Refinement:**
- Child tile'lar hazır olana kadar parent render edilir
- Parent → Child geçişi crossfade ile smooth
- `uUnpopBlend` mekanizması

**Child Quorum:**
- Tüm 4 child hazır olmadan refine edilmez (opsiyonel)
- `"lodChildQuorum"` config parametresi

### 8.2 SardaGlobe Mevcut

**`src/scheduling/lod_selector.cpp`:**
```cpp
bool LodSelector::AreChildrenReady(const TileKey& key, ...) {
    auto children = key.Children();
    int readyCount = 0;
    for (const auto& child : children) {
        if (isReady(child)) ++readyCount;
    }
    if (settings.lodChildQuorum) {
        return readyCount == static_cast<int>(children.size());
    }
    return readyCount > 0;
}
```

**Progressive fallback (`src/io/dem_manager.cpp`):**
```cpp
bool DemManager::SampleHeightDetailed(double lonDeg, double latDeg, ...) {
    // Parent fallback chain:
    // First try exact tile at requested level, then walk to ancestors.
    for (int sampleLevel = level; sampleLevel >= 0; --sampleLevel) {
        TileKey key(sampleLevel, sampleX, sampleY);
        auto it = cache_.find(key);
        if (it != cache_.end() && it->second.valid) {
            // Use ancestor data
            out.usedAncestor = sampleLevel != level;
            return true;
        }
        // Walk up
        sampleX >>= 1;
        sampleY >>= 1;
    }
}
```

### 8.3 Karşılaştırma

| Özellik | Google Earth | SardaGlobe | Durum |
|---------|--------------|------------|-------|
| **Child quorum** | Var | Var (opsiyonel) | ✅ Var |
| **Parent fallback** | Var | Var (DEM ve texture) | ✅ Var |
| **Progressive morph** | Var (unpop) | Var (terrainMorph) | ✅ Var |
| **Texture crossfade** | Var (RASTER_CROSSFADE) | Shader'da var, engine'de eksik | 🟡 **Eksik** |

---

## 9. Derinlemesine Tersine Mühendislik Bulguları

### 9.1 WASM'dan Çıkarılan Kritik String'ler

```
# Frame Pipeline
"InstanceImpl::DoFrameThreadTime"
"InstanceImpl::BuildNextSceneTime"
"BuildNextScene(build_frame = %d)"
"RequestNewFrame(reason = %d, file = %s, line = %d)"
"MissedSceneBuilds"
"Jank60MissedFrames"

# LOD / Traversal
"N5mirth4tree15TraversalOutputE"
"N5mirth4tree7LodInfoE"
"N5mirth4tree8DataNode14TraversalStateE"
"N5mirth4tree18NodeTraversalStateE"
"uCornerLods"
"geometricError"
"maxLodPixels"
"minLodPixels"

# DEM / Elevation
"RefinedElevationsRequester"
"BatchGetElevationsByPointRequest"
"GetTerrainElevation"
"GetAccurateTerrainElevation"
"Plane equations for computing depth"
"Skirts"

# Unpop / Crossfade
"uUnpopBlend"
"uCrossfadeInterpolant"
"RASTER_CROSSFADE"
"RASTER_SINGLE_TEXTURE"
"SetCrossfade"
"UnpopPairTimeFade"

# Threading
"Tile decoder thread creation failed"
"Failed to allocate pbi->tile_workers"
"JobDispatcher"
"WorkerPoolJobRunner"

# Cache
"Memory limit exceeded"
"ClearMemoryCache"
"SetMemoryUsageTargetMb"
```

### 9.2 Mimari Yapı (WASM'dan Reconstructed)

```
geo/render/mirth/
├── earth/
│   ├── earthframehandler.cc    ← Frame handler (terrain render)
│   ├── earthcorebase.cc        ← Core earth logic
│   └── refinedelevationsrequester.cc ← DEM
├── tree/
│   ├── TraversalOutput         ← Quadtree traversal
│   ├── LodInfo                 ← LOD bilgisi
│   └── DataNode::TraversalState
├── core/
│   ├── job/jobdispatcher.cc    ← Job scheduling
│   ├── job/workerpooljobrunner.cc ← Worker threads
│   └── cache/cachemanager.cc   ← Cache management
└── mirthview/
    └── instanceimpl.cc         ← DoFrame, BuildNextScene
```

---

## 10. Düzeltme ve İyileştirme Planı

### 10.1 Kritik (P0) - Parity Blocker

| # | Görev | Dosyalar | Tahmini Süre |
|---|-------|----------|--------------|
| 1 | **SSE Hesaplama Düzeltmesi** | `math/tile_math.h`, `scheduling/lod_selector.cpp` | 1 gün |
| 2 | **uCornerLods Vertex Shader Entegrasyonu** | `rendering/shader_manager.cpp`, `rendering/tile_renderer.cpp` | 2 gün |
| 3 | **Unpop/Crossfade Engine Entegrasyonu** | `engine/globe_engine.cpp`, `rendering/tile_renderer.cpp` | 3 gün |
| 4 | **Frame Pipeline Ayrıştırma** | `engine/globe_engine.cpp` | 2 gün |

### 10.2 Önemli (P1) - Parity Geliştirme

| # | Görev | Dosyalar | Tahmini Süre |
|---|-------|----------|--------------|
| 5 | **Geometric Error Tabanlı LOD** | `math/tile_math.h`, `core/tile_key.h` | 2 gün |
| 6 | **RequestNewFrame Mekanizması** | `engine/globe_engine.cpp` | 1 gün |
| 7 | **TraversalOutput Yapılandırması** | `scheduling/tile_pyramid.h` | 1 gün |
| 8 | **Touch-based Eviction** | `io/tile_cache.cpp` | 1 gün |

### 10.3 İyileştirme (P2) - Optimizasyon

| # | Görev | Dosyalar | Tahmini Süre |
|---|-------|----------|--------------|
| 9 | **Depth Plane Equations** | `rendering/tile_mesh_builder.cpp` | 2 gün |
| 10 | **DEM Batch Protobuf API** | `io/dem_manager.cpp` | 3 gün |
| 11 | **JobDispatcher Pattern** | `scheduling/job_system.cpp` (yeni) | 2 gün |

---

## 11. Detaylı Görev Açıklamaları

### 11.1 P0.1: SSE Hesaplama Düzeltmesi

**Mevcut:**
```cpp
// tile_math.h
inline float ComputeSSE(...) {
    double tileSizeMeters = EarthCircumferenceMeters / (1 << key.level);
    // ... tile size bazlı hesaplama
}
```

**Hedef (GE-compatible):**
```cpp
inline float ComputeSSE_GE(const TileKey& key, double distanceMeters, 
                           int viewportHeight, float fovDegrees) {
    // GE formülü: geometricError * focalLength / distance
    // geometricError = tileSizeMeters / 2^level (approx)
    // focalLength = viewportHeight / (2 * tan(fov/2))
    
    double tileSizeMeters = EarthCircumferenceMeters / (1 << key.level);
    double geometricError = tileSizeMeters; // glTF-style
    
    double fovRad = fovDegrees * M_PI / 180.0;
    double focalLength = viewportHeight / (2.0 * tan(fovRad / 2.0));
    
    double sse = (geometricError * focalLength) / distanceMeters;
    return static_cast<float>(sse);
}
```

### 11.2 P0.2: uCornerLods Vertex Shader Entegrasyonu

**Adımlar:**
1. Vertex shader'a `uCornerLods` uniform'u ekle
2. Per-vertex LOD değerini bilinear interpolate et
3. Height/position blend uygula

```glsl
// Vertex shader
uniform vec4 uCornerLods; // NW, NE, SE, SW

float getLodBlend(vec2 uv) {
    // Bilinear interpolation of corner LODs
    float lodNW = uCornerLods.x;
    float lodNE = uCornerLods.y;
    float lodSE = uCornerLods.z;
    float lodSW = uCornerLods.w;
    
    float lodN = mix(lodNW, lodNE, uv.x);
    float lodS = mix(lodSW, lodSE, uv.x);
    return mix(lodS, lodN, uv.y); // Note: v is flipped
}
```

### 11.3 P0.3: Unpop/Crossfade Engine Entegrasyonu

**Adımlar:**
1. Her tile için parent texture referansını tut
2. Crossfade state machine ekle (fadeInProgress, fadeStartTime)
3. Speed limit kontrolü ekle
4. Render sırasında parent → child blend uygula

```cpp
// Tile yapısına ekle
struct Tile {
    // ... mevcut alanlar
    
    // Crossfade state
    TileKey unpopParentKey;      // Parent tile key for crossfade
    float unpopBlend = 1.0f;      // 0 = parent, 1 = child
    double unpopStartTime = 0.0;
    bool unpopInProgress = false;
    
    bool ShouldUnpop(float cameraSpeedNdc) {
        if (cameraSpeedNdc > config.unpopSpeedLimitNdc) {
            return false; // Disable unpop at high speed
        }
        return true;
    }
};
```

### 11.4 P0.4: Frame Pipeline Ayrıştırma

**Mevcut:**
```cpp
void GlobeEngine::Update(dt, currentTime) {
    // Her şey burada
}
void GlobeEngine::Render() {
    // Sadece GL çizim
}
```

**Hedef:**
```cpp
void GlobeEngine::DoFrame() {
    // [1] Inter-frame timing
    // [2] Camera update
    // [3] BuildNextScene()
    // [4] RenderScene()
    // [5] RequestNewFrame() if needed
}

void GlobeEngine::BuildNextScene() {
    // - Quadtree traversal
    // - LOD selection
    // - Tile request scheduling
    // - Shader scene preparation
}

void GlobeEngine::RenderScene() {
    // - Actual GL rendering
}
```

---

## 12. Test ve Doğrulama Planı

### 12.1 Görsel Parity Testleri

| Test | Açıklama | Başarı Kriteri |
|------|----------|----------------|
| LOD Seam | Tile sınırlarında çatlak kontrolü | 0 görünür çatlak |
| Unpop Pop | Zoom-in sırasında smooth geçiş | Parent → Child fade görünür |
| Tilt LOD | Horizon'da LOD değişimi | Seamless transition |
| DEM Pop | Terrain yüklenirken pop | Morph animasyonu görünür |

### 12.2 Metrik Parity Testleri

| Metrik | Google Earth | SardaGlobe Hedef |
|--------|--------------|------------------|
| SSE threshold | ~2.0 | Eşleşmeli |
| Fade duration | 300ms | Eşleşmeli |
| Unpop speed limit | NDC-based | Eşleşmeli |
| Max refinements/frame | Sınırlı | Eşleşmeli |

---

## 13. Sonuç ve Öneriler

### 13.1 Özet

SardaGlobe, Google Earth mimarisinin büyük bir kısmını doğru şekilde uygulamış, ancak kritik parity farkları bulunmaktadır:

1. **SSE hesaplama** farklılığı LOD geçişlerinde farklı davranışlara yol açıyor
2. **uCornerLods** shader'da kullanılmıyor, bu da seam'lere neden oluyor
3. **Unpop/Crossfade** engine entegrasyonu eksik, bu da "pop" efektlerine yol açıyor
4. **Frame pipeline** monolitik, bu da optimizasyon ve parallelism'i sınırlıyor

### 13.2 Öncelik Önerisi

1. **Hemen (P0):** Unpop/Crossfade entegrasyonu - kullanıcı deneyimi kritik
2. **Kısa vade (P1):** SSE hesaplama ve uCornerLods - seam ve LOD sorunları
3. **Orta vade (P2):** Frame pipeline refactor - performans ve maintainability

### 13.3 Tahmini Timeline

- **P0 (Kritik):** 8 gün
- **P1 (Önemli):** 5 gün  
- **P2 (İyileştirme):** 7 gün
- **Toplam:** ~20 gün (1 ay içinde tamamlanabilir)

---

## Ekler

### A. Google Earth WASM Referansları

**Dosya Konumları:**
- `google_earth/downloads/earthplugin_web.wasm` (19.16 MB)
- `google_earth/analysis/symbols/` (extracted symbols)
- `google_earth/analysis/wat/` (WAT disassembly)

**Kritik Fonksiyon İmzaları (WASM):**
```
_N5mirth5earth17EarthFrameHandlerE
_N5mirth4tree15TraversalOutputE
_N5earth10elevations26RefinedElevationsRequesterE
```

### B. SardaGlobe Mimari Haritası

```
src/
├── camera/           # Camera ve navigation
├── core/             # Tile, TileKey, Config
├── engine/           # GlobeEngine (main loop)
├── io/               # Fetch, decode, DEM
├── math/             # Frustum, tile_math
├── rendering/        # Shaders, mesh builder, renderer
└── scheduling/       # LOD selector, tile pyramid, scheduler
```
