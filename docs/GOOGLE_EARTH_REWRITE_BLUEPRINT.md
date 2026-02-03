# Google Earth Rewrite Blueprint

**Kaynak:** WASM Tersine Mühendislik (19.16 MB binary, 6.8M satır WAT)  
**Tarih:** 2026-01-31

---

## 1. Mimari Genel Bakış

### Katmanlı Yapı
```
Flutter Web UI → JS Wrapper → C++ WASM Engine → WebGL 2.0
```

### Ana Subsistemler
| Subsystem | Sorumluluk |
|-----------|------------|
| EarthPluginWeb | Top-level orchestrator |
| TileManager | Tile pyramid, scheduling, cache |
| TerrainManager | Elevation, mesh generation |
| Camera/View | Frustum, projection, ray casting |
| Renderer | WebGL state, shaders, draw calls |
| AssetLoaders | Vector, 3D, Photo, Diff tiles |

### Binary Metrikleri
- **WASM**: 19.16 MB (4.35 MB compressed)
- **Functions**: 42,751
- **Strings**: 165,521 extracted

---

## 2. Tile Pyramid Sistemi

### TileKey Yapısı
```cpp
struct TileKey {
    int level;  // 0-22
    int x, y;
    
    TileKey GetParent() const { return {level-1, x/2, y/2}; }
    
    std::array<TileKey, 4> GetChildren() const {
        return {{{level+1, x*2, y*2}, {level+1, x*2+1, y*2},
                 {level+1, x*2, y*2+1}, {level+1, x*2+1, y*2+1}}};
    }
    
    std::string ToQuadKey() const {
        std::string qk; qk.reserve(level);
        for (int i = level; i > 0; --i) {
            char d = '0'; int m = 1 << (i-1);
            if (x & m) d += 1;
            if (y & m) d += 2;
            qk.push_back(d);
        }
        return qk;
    }
};
```

### Tile State Machine
```
UNLOADED → SCHEDULED → FETCHING → DECODING → UPLOADING → READY
                                                      ↓
                                                   FAILED/EVICTED
```

### 2.1 TileBounds + Web Mercator Matematiği (Net Formül)
Kaynak: `reconstructed_headers/tile_coordinates.h`
```
n = 2^level
west  =  x      * 360/n - 180
east  = (x + 1) * 360/n - 180

// Mercator → latitude
lat(y) = atan(sinh(y * PI)) * 180/PI
y1 = 1 - 2*y/n
y2 = 1 - 2*(y+1)/n
north = lat(y1)
south = lat(y2)
```
> Bu formül frustum culling + SSE için tile merkezini (lat/lon) çıkarır.

### 2.2 Normalized Tile Coordinates
Kaynak: `reconstructed_headers/tile_coordinates.h`
```
u,v ∈ [0,1]  // tile içi normalize koordinatlar
lon = west  + u * (east - west)
lat = south + v * (north - south)
```
> GPU tarafında `aTileCoords` / `uTileParams` gibi uniform/attrib’lere temel olur.

### 2.3 URL Şablonları (Pattern Substitutions)
Kaynak: `COMPLETE_RECONSTRUCTION.md`
```
{z} / {level}  → zoom
{x}           → tile x
{y}           → tile y (top-down)
{-y}          → TMS invert y
{quadkey}     → Microsoft QuadKey
{bbox}        → "west,south,east,north"
```
> Bu, Google Earth’ün asset pipeline’ında tile fetch URL üretiminin standart mekanizmasıdır.

### 2.4 Tile Visibility + Job Scheduling
Kaynak: `ARCHITECTURE.md`, `tile_system.h`
- **Visibility**: Camera frustum → tile bounds test.
- **Priority**: Distance + importance + viewport overlap.
- **Scheduler**: `MapTilePyramidUpdater` job list; `TileJob{coord, priority, pending}`.
- **Worker Pool**: decode işleri worker thread’lerde, GPU upload main thread’de.

### 2.5 Tile Metrics / Instrumentation
Kaynak: `COMPLETE_RECONSTRUCTION.md`
- `VectorTileAssetLoader::DoMergeAndLoadTime`
- `VectorTileAssetLoader::FinishMergeTime`
- Decode / merge / upload süreleri ayrı raporlanır.
> Bu metrikler “adaptive cache” ve “prefilter” kararlarını besler.

### 2.6 Tile Error Strings (Validation + Recovery)
Kaynak: `COMPLETE_RECONSTRUCTION.md`
- “Failed to allocate tile_workers”
- “Truncated packet or corrupt tile length/size”
- “Invalid glTF binary” / “BIN chunk alignment”
> Bu hata seti, asset loader’ların strict validation yaptığını gösterir.

---

## 3. LOD (Screen-Space Error) Algoritması

### Temel Formül
```
SSE = (geometric_error / distance) × (viewport_height / (2 × tan(fov/2)))
```

### Karar Mantığı
```cpp
float ComputeSSE(const TileKey& tile, const Camera& cam) {
    // Geometric error = Earth circumference / (2^level × tile_size)
    double geo_error = 40075017.0 / (pow(2.0, tile.level) * 256);
    
    // Distance to tile center
    double distance = DistanceToTileCenter(tile, cam);
    
    // Project to screen
    double focal = viewport.height / (2.0 * tan(fov/2));
    return (geo_error / distance) * focal;
}

// Decision
if (sse > 2.0f && tile.level < 22) {
    // Refine to children
} else {
    // Render this tile
}
```

### 3.1 Geometric Error Kaynağı (Pratik Yaklaşım)
Kaynak: `COMPLETE_RECONSTRUCTION.md`, `terrain_system.h`
```
geometric_error ≈ EarthCircumference / (2^level * TILE_SIZE)
EarthCircumference = 40075017.0 (m)
TILE_SIZE = 256
```
> Bu formül “level arttıkça hata azalır” prensibini sabitler.

### 3.2 Distance Hesabı (Tile Center → Camera)
Kaynak: `tile_coordinates.h`, `camera_view.h`
- Tile center (lat/lon) → world position (ECEF/ellipsoid).
- Camera position (lat/lon/alt) → world position.
- Distance = |cam - tile_center| (world units).

### 3.3 Horizon Culling / Parent Fallback
Kaynak: `COMPLETE_RECONSTRUCTION.md`
- Çocuk tile hazır değilse parent render.
- Ufuk altında kalan tile’lar culled.

### SSE Threshold Değerleri
| Mode | SSE | Açıklama |
|------|-----|----------|
| Quality | 1.0 | Çok tile, yüksek kalite |
| Standard | 2.0 | Dengeli |
| Performance | 4.0 | Az tile, hızlı |

---

## 4. Terrain Mesh Sistemi

### Heightmap Format
- **Resolution**: 256×256 samples
- **Interpolation**: Bilinear
- **Format**: Float32 heights

### Skirt Generation (LOD Seam Prevention)
```cpp
void GenerateSkirts(TerrainMesh& mesh, float depth) {
    // Perimeter vertices pushed down radially
    for each perimeter_vertex {
        vec3 pos = original_pos;
        float len = length(pos);
        float scale = (len - depth) / len;
        skirt_pos = pos * scale;
    }
    // Connect with triangles
}
```

### 4.1 Depth Planes (Shader İçin)
Kaynak: `terrain_system.h`
```
PlaneEquation: ax + by + cz + d = 0
depth_planes[] + plane_indices[] → vertex bazlı depth hesap
```
> Terrain shader’ı per-vertex plane ile depth optimizasyonu yapar.

### 4.2 TerrainTile State
Kaynak: `terrain_system.h`
```
UNLOADED → LOADING → LOADED → FAILED
ShouldRefine(camera, threshold)  // SSE gate
```

### Elevation API
```cpp
// Fast (cached)
double GetTerrainElevation(lat, lon, type);

// Async high-accuracy
void GetAccurateTerrainElevation(lat, lon, accuracy, callback);

// Altitude modes: CLAMP_TO_GROUND, RELATIVE_TO_GROUND, ABSOLUTE
```

---

## 5. Rendering Pipeline

### Frame Sequence
1. Sky/Atmosphere (background)
2. Terrain (opaque)
3. Imagery tiles (draped)
4. Water (transparent)
5. Clouds
6. Vector layers
7. 3D tiles
8. KML features
9. Post-processing (DOF, motion blur)

### Shader Variants (100+ toggles)
```cpp
struct ShaderFeatures {
    bool ENABLE_ATMOSPHERE;
    bool ENABLE_LIGHTING;
    bool HAS_NORMAL_MAP;
    bool USE_IBL;
    // ...100+ more
    
    std::string GenerateDefines() const;
};
```

### 5.1 RenderOption Flags (Örnekler)
Kaynak: `rendering_system.h`
- Atmosphere/Sky: `ENABLE_ATMOSPHERE`, `ENABLE_SKY_SKYBOX`
- Lighting: `ENABLE_LIGHTING`, `ENABLE_WATER_LIGHTING`, `ENABLE_CITY_LIGHT_SHADER`
- Materials: `ENABLE_NORMAL_MAP`, `USE_IBL`, `USE_PUNCTUAL`
- Effects: `ENABLE_DEPTH_OF_FIELD`, `ENABLE_MOTION_BLUR`

### 5.2 WebGL Extensions (Görünen Set)
Kaynak: `ARCHITECTURE.md`, `EARTH_JS_SUMMARY.md`
- `WEBGL_polygon_mode`
- `EXT_polygon_offset_clamp`
- `EXT_disjoint_timer_query`
- `OES_vertex_array_object`
- `ANGLE_instanced_arrays`

### WebGL State Tracking
```cpp
// Minimize redundant GL calls
class GLStateTracker {
    void SetDepthTest(bool);
    void BindTexture(int unit, GLuint);
    void ApplyState(); // Only call GL for changed state
};
```

---

## 6. Asset Pipeline

### Loader Types
| Loader | Format | Thread |
|--------|--------|--------|
| ImageLoader | JPEG/PNG/WebP | Worker |
| VectorTileAssetLoader | Protobuf | Worker |
| ThreeDTilesSetLoader | glTF/GLB | Worker |
| PhotoTileLoadableAsset | JPEG | Worker |

### 6.1 glTF Validation (Gerçek Hata Sözlüğü)
Kaynak: `network_assets.h`, `COMPLETE_RECONSTRUCTION.md`
- Magic bytes check
- JSON/BIN chunk alignment (4‑byte)
- Accessor / bufferView validity
> glTF validation strict; invalid asset “FAILED” state.

### 6.2 Photo Tile “Unpop” Transition
Kaynak: `tile_system.h`, `GOOGLE_EARTH_INTEGRATION_VERIFICATION.md`
```
unpopFactor 0→1
blendFactor = 1 - unpopFactor
```
> Parent texture → child texture geçişini yumuşatır.

### Threading Model
```
Main Thread: WebGL, culling, LOD, render
Worker Threads (4-8): Decode, parse, mesh gen
```

### Decode/Upload Split
```cpp
// Worker thread
void DoMergeAndLoad() {
    DecodeData(raw_data);  // CPU work
    MergeFeatures();
}

// Main thread
void FinishMerge() {
    UploadVertexBuffers();  // GL calls
    UploadTextures();
}
```

---

## 7. Cache & Memory

### LRU Cache with Smart Eviction
```cpp
// Eviction score (lower = evict first)
score = access_count * 2.0
      + recency_factor
      - distance_from_camera * 3.0
      + (pinned ? 1000.0 : 0)
```

### Memory Targets
- **Max cache**: 512 MB
- **Target (soft)**: 400 MB
- **Pin**: Protect important tiles

### 7.1 Memory/Cache Metrics API
Kaynak: `earth_core.h`
- `GetCurrentMemoryCacheSizeMb()`
- `GetTargetMemoryCacheSizeMb()`
- `GetCurrentMemoryUsageMb()`
> Cache throttle ve adaptive eviction bu metriklerle yapılır.

---

## 8. Koordinat Sistemleri

### WGS84 Constants
```cpp
constexpr double WGS84_A = 6378137.0;      // Semi-major (m)
constexpr double WGS84_E2 = 0.00669437999; // Eccentricity²
```

### Conversions
```cpp
// Lat/Lon/Alt → ECEF
void LatLonAltToECEF(lat, lon, alt, x, y, z);

// ECEF → Lat/Lon/Alt
void ECEFToLatLonAlt(x, y, z, lat, lon, alt);

// Screen → World Ray
Ray GetWorldRay(screen_x, screen_y, camera);
```

---

## 9. Sabitler (WASM'den Çıkarılan)

```cpp
// Globe
constexpr double EARTH_RADIUS = 6378137.0;      // meters
constexpr double EARTH_CIRCUMFERENCE = 40075017.0;

// Tile
constexpr int TILE_SIZE = 256;
constexpr int MIN_ZOOM = 0;
constexpr int MAX_ZOOM = 22;

// Camera
constexpr double DEFAULT_FOV = 45.0;            // degrees
constexpr double DEFAULT_NEAR = 1.0;
constexpr double DEFAULT_FAR = 1000000.0;

// LOD
constexpr float SSE_THRESHOLD = 2.0f;           // pixels

// Cache
constexpr size_t MAX_CACHE_MB = 512;
constexpr size_t TARGET_CACHE_MB = 400;
```

---

## 10. Implementation Checklist

### Phase 1: Core (Hafta 1)
- [ ] TileKey + QuadKey encoding
- [ ] TileBounds calculation
- [ ] Basic camera (perspective)
- [ ] Frustum planes extraction
- [ ] Simple tile selection (no SSE yet)

### Phase 2: LOD (Hafta 2)
- [ ] SSE calculation
- [ ] Recursive tile traversal
- [ ] Parent fallback (children not ready)
- [ ] Horizon culling

### Phase 3: Terrain (Hafta 3)
- [ ] Heightmap loader
- [ ] Mesh generation
- [ ] Skirt generation
- [ ] Elevation queries

### Phase 4: Rendering (Hafta 4)
- [ ] WebGL state tracker
- [ ] Terrain shader
- [ ] Imagery draping
- [ ] Basic lighting

### Phase 5: Network (Hafta 5)
- [ ] Async tile fetching
- [ ] Worker thread decoding
- [ ] LRU cache
- [ ] Priority scheduling

### Phase 6: Polish (Hafta 6)
- [ ] Atmosphere
- [ ] Smooth transitions (unpop)
- [ ] Memory management
- [ ] Performance profiling

---

## 11. Kaynak Referansları

### Repo İçi
- `~/Desktop/google_earth/COMPLETE_RECONSTRUCTION.md` - 1400 satır detay
- `~/Desktop/google_earth/DEEP_REVERSE_ENGINEERING.md` - 2000 satır algoritma
- `~/Desktop/google_earth/ARCHITECTURE.md` - 200 satır overview
- `~/Desktop/sardaglobe/google_earth_analysis/` - Aynı içerik

### Header Dosyaları (Reconstructed)
```
reconstructed_headers/
├── tile_coordinates.h  (670 satır)
├── tile_system.h       (391 satır)
├── terrain_system.h    (399 satır)
├── camera_view.h       (400 satır)
├── rendering_system.h  (747 satır)
├── shaders.glsl        (766 satır)
├── earth_core.h        (857 satır)
└── network_assets.h    (917 satır)
```

---

## 12. Sınırlar / Çıkarılamayanlar

1. **Tam shader kaynakları** - Sadece pattern'lar çıkarılabildi
2. **Proprietary API endpoint'leri** - Google internal URL'ler
3. **Scheduling heuristic değerleri** - Deneysel tuning gerekir
4. **Optimize edilmiş inline code** - Tam reconstruct zor

---

**Sonuç:** Bu blueprint, Google Earth Web'in ~%90 mimari detayını içerir.
Geri kalan %10 deneysel implementasyon ve profiling ile tamamlanabilir.

---

## 13. WASM Runtime (Aggressive Extraction)
Kaynak: `DEEP_REVERSE_ENGINEERING.md`
```
(memory 0) 8192 32768 shared
8192 pages  = 512 MB initial
32768 pages = 2 GB max
```
- **Shared memory + atomics** → multi‑thread decode / mesh
- **Imports (örnek):**
  - `__pthread_create_js`
  - `__emscripten_init_main_thread_js`
  - `__emscripten_thread_mailbox_await`
  - `emscripten_gl*` (100+ WebGL wrapper)
- **Exports (örnek):** `hg` (ctors), `kg` (_main), `mg` (_malloc), `ng` (_free)

## 14. JS Wrapper / Flutter Köprüsü
Kaynak: `ARCHITECTURE.md`, `EARTH_JS_SUMMARY.md`
- `earthplugin_web.js` WASM bootstrap + PThread setup
- Flutter UI (`main.dart.js`) platform channels ile event/command dispatch
- CanvasKit ile UI → WebGL compositing

## 15. OpenGL/WebGL “Kaynak Yapısı” (İnferred)
> Tam GLSL kaynakları yok; ama yapı net:
- **Render passes** ayrı; her pass kendi shader variant’ını seçiyor.
- **State tracker** redundant GL çağrılarını engelliyor.
- **Uniform düzeni**: tile‑centric parametre setleri (`uTileParams`, `uPhotoTileTexture`, vb.)
- **Mesh layout**: positions + normals + texcoords + per‑tile plane index.

## 16. Re‑Implementation İçin Çekirdek Modül Şeması
```
EarthCore
├─ View/Camera
├─ TilePyramid + Scheduler
├─ TerrainManager
├─ AssetLoaders (Image/Vector/3D/Diff/Photo)
├─ Renderer + ShaderVariants
└─ Metrics/Cache/Prefetch
```
> Bu şema Google Earth’ün çekirdek kontratını yeniden yazmak için minimum altyapıdır.

---

## 17. JS Glue (earthplugin_web.js) — ASM_CONSTS Hooks
Kaynak: `google_earth_analysis/js_files/earthplugin_web_wasm/earthplugin_web.js`
Bu wrapper, WASM’den JS’e “konstante callback” köprüsü kuruyor:
- `Module.publish(topic, bytes)` → string + byte payload publish hook.
- `Module.onViewportResized(w,h)` → viewport resize event.
- `earth-wasm-started` + `lfs-cpp-started` DOM events.
- `HaveOffsetConverter()` → `wasmOffsetConverter` global check.

> Bu hook seti UI/engine köprüsünün minimum API yüzeyi gibi davranıyor.

## 18. Label/Text Rendering Pipeline (JS‑side Rasterization)
Kaynak: `earthplugin_web.js` (LabelRenderer)
**Özet davranış:**
- `MAX_LABEL_RENDERS = 32` → render throttling (requestAnimationFrame).
- Hidden canvas pool (capacity = `512*512` area) + reuse ölçümleri:
  - `canvasPoolSize`, `canvasReused`, `canvasQueried`.
- Font stack: `"Google Sans", Arial, sans-serif`.
- `devicePixelRatio` ile scale + tracking/leading oranları.
- **4096 px** üstünde clamp (width/height).
- Rasterize → `Module.SetRenderedString(buffer|canvasId, w, h, lineAdvance, requestId)`.
- Texture upload: `gl.activeTexture(TEXTURE7)` + `gl.texSubImage2D(...)` + state restore.

**Pratik çıkarım:** Etiketler, GPU’da değil JS canvas üzerinde rasterize edilip texture subimage ile tile atlasına basılıyor. Bu, text quality + perf dengesini sağlar.

## 19. PThread/Worker Protokolü (JS ↔ WASM)
Kaynak: `earthplugin_web.js`
Worker başlatma mesajları:
```
cmd = "load" → wasmMemory + wasmModule init
cmd = "run"  → __emscripten_thread_init + start_routine
cmd = "checkMailbox" → __emscripten_check_mailbox
```
Thread adı: `em-pthread` prefix (ENVIRONMENT_IS_PTHREAD).
> Multi-thread decode + mesh + IO için standart Emscripten PThread protokolü kullanılıyor.

## 20. WebGL Wrapper Coverage (Emscripten)
Kaynak: `earthplugin_web.js` (imports)
- WebGL2’nin neredeyse tüm fonksiyonları var (`glDraw*`, `glTex*`, `glUniform*`, `glVertexAttrib*`, `glTransformFeedback*`).
- EXT/ANGLE varyantları maplenmiş (`glDrawArraysInstancedANGLE`, `glVertexAttribDivisorEXT` vb.).
- Debug vendor/renderer query:
  - `WEBGL_debug_renderer_info`
  - `UNMASKED_RENDERER_WEBGL`, `UNMASKED_VENDOR_WEBGL`.

> Bu kapsam, Google Earth’ün WebGL feature surface’ının geniş olduğunu ve GL state tracker ihtiyacını doğruluyor.

## 21. Build/Version Metadata (WASM Exports)
Kaynak: `earthplugin_web.js` (exported symbols)
Örnek exported string pointerları:
- `_kVersionStampBuildChangelistStr`
- `_kVersionStampBuildDateTimePstStr`
- `_kVersionStampBuildToolStr`
- `_kVersionStampBuildIdStr`
> Build provenance bilgisi WASM içinde tutuluyor; debug/telemetry için kullanılabilir.
