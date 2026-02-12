# SardaGlobe — Tile, DEM & Render Teknik Referans ve Geliştirme Planı

> **Birleştirilmiş Doküman** (2026-02-12)
> 3 kaynak birleştirildi: GE Derin Analiz + 3D Terrain Planı + Tile Pipeline Optimizasyon Planı

**Yapı:**
- **BÖLÜM A (§1-14):** Google Earth WASM Referans Mimarisi
- **BÖLÜM B (§15):** 3D Terrain Geliştirme Planı (FAZ 0-5)
- **BÖLÜM C (§16):** Tile Pipeline Optimizasyon Planı (P0-P6)

> **WASM Kaynak:** `google_earth/wasm_files/earthplugin_web.wasm` (19.16MB, 42,751 internal function + 384 import + 49 export)
> **Yöntem:** WAT disassembly + string analizi + mangled name demangling + kaynak yol analizi

---

# BÖLÜM A: Google Earth WASM Referans Mimarisi

## 1. Genel Mimari Harita

Google Earth'ün render motoru **"mirth"** adlı bir iç engine üzerine kurulu.
Kaynak dosya yolları bunu açıkça gösteriyor:

```
geo/render/mirth/
├── earth/                    ← Globe-spesifik (terrain, atmosphere, rocktree)
│   ├── earthframehandler.cc  ← ANA FRAME HANDLER
│   ├── atmosphere.cc
│   ├── rocknode.cc
│   ├── rockmeshmanager.cc
│   ├── cubemaptexturemanager.cc
│   ├── exposurecontroller.cc
│   └── datedrocknodemanager.cc
├── map/                      ← 2D tile sistemi (vector, raster)
│   ├── vectortilemanager.cc
│   ├── grayscaleimageutils.cc
│   └── polygonbuilder.cc
├── tree/                     ← Quadtree traversal
│   ├── TraversalOutput
│   ├── DataNode::TraversalState
│   ├── NodeTraversalState
│   └── LodInfo
├── core/
│   ├── render/
│   │   ├── rendercontextmanager.cc
│   │   ├── textureatlasmanager.cc
│   │   ├── model/gltfmodelgeometry.cc
│   │   ├── label/labellayout.cc
│   │   ├── label/geosurfacelabels.cc
│   │   └── video/video.cc, videotexture.cc, videosync.cc
│   ├── cache/
│   │   ├── cachemanager.cc
│   │   └── fetch/fetchmanager.cc
│   ├── base/
│   │   ├── job/jobdispatcher.cc
│   │   ├── job/workerpooljobrunner.cc
│   │   └── types/framestatustracker.cc
│   └── kmlimpl/              ← KML rendering
├── camera/
│   ├── camerasourcefactoryimpl.cc
│   ├── camerautilsimpl.cc
│   └── cameramanipulators/
├── mirthview/
│   ├── instanceimpl.cc       ← INSTANCE IMPL (DoFrame, BuildNextScene)
│   ├── viewimpl.cc
│   ├── windowimpl.cc
│   └── worldimpl.cc
├── photo/                    ← Street View / Photo tiles
│   ├── photoframehandler.cc
│   ├── photomeshmanager.cc
│   └── fader.cc
├── net/                      ← VFS / zipasset hattı (string kanıtı)
│   ├── VfsRequestHandler     ← "mirth-vfs://..."
│   ├── ZipRequestHandler     ← "zipasset://..."
│   └── ZipAssetManager       ← "__asset_manifest__.txt" + "PK"
├── solarsystem/
│   ├── solarsystemframehandler.cc
│   └── ...
└── gridlines/
    └── gridlinemanagerimpl.cc
```

**Katmanlı Mimari:**
```
┌─────────────────────────────────────────────────────┐
│  earth (app layer)                                  │
│  geo/earth/app/cpp/core/earthcorebase.cc            │
│  ├── EarthCoreBase::OnFirstScene                    │
│  ├── Camera, Settings, Document managers            │
│  └── ElevationQueryProcessor                        │
├─────────────────────────────────────────────────────┤
│  mirth (render engine)                              │
│  ├── InstanceImpl   (DoFrame, BuildNextScene)       │
│  ├── EarthFrameHandler (per-frame earth logic)      │
│  ├── Tree traversal (QuadTree LOD)                  │
│  ├── ShaderScene, Renderer, Picker                  │
│  └── Cache/Fetch pipeline                           │
├─────────────────────────────────────────────────────┤
│  ion (low-level graphics)                           │
│  geo/render/ion/gfx/graphicsmanager.cc              │
│  └── Buffer, Texture, Shader management             │
└─────────────────────────────────────────────────────┘
```

---

## 2. Frame Render Döngüsü (DoFrame Pipeline)

### 2.1. Timing Metrikleri (WASM'dan çıkarılan isimler)

```
InterFrameTime                        ← İki DoFrame arası süre
LastDoFrameTime                       ← Son DoFrame süresi  
MaxTotalFrameTime                     ← Maximum frame süresi
InstanceImpl::DoFrameThreadTime       ← DoFrame thread süresi
InstanceImpl::BuildNextSceneTime      ← Scene build süresi
DoFrameCallCount                      ← Toplam DoFrame çağrı sayısı
MissedSceneBuilds                     ← Kaçırılan scene build sayısı
Jank60MissedFrames                    ← 60fps'de kaçırılan frame
Jank30MissedFrames                    ← 30fps'de kaçırılan frame
```

### 2.2. Ana Frame Döngüsü (Reconstructed)

```
_main (func 32863, ~60 call/sec)
│
├── InstanceImpl::DoFrame()
│   │
│   ├── [1] InterFrameTime hesapla (son frame'den bu yana geçen süre)
│   │
│   ├── [2] DoFrame_thread (ayrı thread'de)
│   │   ├── Camera::Update()
│   │   │   ├── CameraManager update
│   │   │   ├── MapCameraManipulatorHandler input
│   │   │   └── SetTraversalCamera() ← traversal kamerası ayarla
│   │   │
│   │   ├── RunLoaders [delayed]
│   │   │   ├── VectorTileAssetLoader::DoMergeAndLoad()
│   │   │   ├── DiffTileAssetLoader decode
│   │   │   ├── ThreeDTilesSetLoader decode
│   │   │   ├── RockNodeSetLoader decode
│   │   │   ├── RockMeshLoader decode
│   │   │   ├── PaintParametersAsset decode
│   │   │   └── GlobalStyleTableAsset decode
│   │   │
│   │   └── Main-thread callback kuyruğuna closure schedule
│   │
│   ├── [2b] Main-thread callback drain
│   │   ├── VectorTileAssetLoader::UpdateExpirationsOnMainThread()
│   │   └── VectorTileAssetLoader::FinishMerge() ← GPU upload (main thread)
│   │
│   ├── [3] InstanceImpl::BuildNextScene(build_frame)
│   │   ├── SetTraversalViewport(left, top, width, height)
│   │   │
│   │   ├── ★ QUADTREE TRAVERSAL ★
│   │   │   ├── TraversalOutput hesapla
│   │   │   ├── DataNode::TraversalState güncelle
│   │   │   ├── NodeTraversalState değerlendir
│   │   │   ├── LodInfo ile LOD seçimi
│   │   │   │   ├── uCornerLods ← "Tile corner lods for bilinear interp."
│   │   │   │   ├── geometricError hesapla
│   │   │   │   ├── SSE (Screen Space Error) = geometricError / distance * focalLength
│   │   │   │   └── maxLodPixels / minLodPixels kontrol
│   │   │   │
│   │   │   └── Visible tile listesi oluştur
│   │   │
│   │   ├── Tile Request Scheduling
│   │   │   ├── AssetNetLoads ← network'ten yükleme
│   │   │   ├── AssetDiskLoads ← disk cache'den yükleme  
│   │   │   └── Priority queue güncelle
│   │   │
│   │   └── ShaderScene oluştur (render komutları)
│   │
│   ├── [4] Render Pipeline
│   │   ├── EarthFrameHandler::OnFrame()
│   │   │   ├── RenderTerrain
│   │   │   │   ├── Raster texture bind
│   │   │   │   ├── RASTER_SINGLE_TEXTURE / RASTER_CROSSFADE
│   │   │   │   ├── NOTERRAIN_TEXTURE (terrain olmayan yerler)
│   │   │   │   ├── ENABLE_TERRAIN_ATMOSPHERE_NOSCATTER
│   │   │   │   ├── SKIRTS oluştur
│   │   │   │   └── Depth plane hesapla
│   │   │   │
│   │   │   ├── RenderAtmosphere
│   │   │   │   ├── ENABLE_ATMOSPHERE
│   │   │   │   └── Inscatter / DISABLE_INSCATTER
│   │   │   │
│   │   │   ├── RenderWater
│   │   │   │   ├── ENABLE_IMPROVED_WATER
│   │   │   │   ├── ENABLE_WATER_LIGHTING
│   │   │   │   └── ENABLE_UNDER_WATER_LIGHTING
│   │   │   │
│   │   │   ├── RenderSky
│   │   │   │   ├── ENABLE_SKY_NOSCATTER
│   │   │   │   ├── ENABLE_SKY_SKYBOX
│   │   │   │   └── Star/Moon/Eclipse shaders
│   │   │   │
│   │   │   ├── RenderClouds
│   │   │   │   ├── ENABLE_CLOUD_SHADER
│   │   │   │   └── ENABLE_CLOUD_SHADOWS
│   │   │   │
│   │   │   └── Render3DTiles (RockTree/BuiltEnv)
│   │   │
│   │   ├── PhotoFrameHandler::OnFrame() (Street View)
│   │   └── SolarSystemFrameHandler::OnFrame()
│   │
│   ├── [5] RequestNewFrame(reason, file, line)
│   │   └── Yeni frame gerekiyorsa tekrar schedule et
│   │
│   └── [6] SwapBuffers / Present
│
└── Worker Threads
    ├── Tile decoder threads
    │   └── "Tile decoder thread creation failed"
    ├── vpx tile workers (video decode)
    │   └── "Failed to allocate pbi->tile_workers"
    └── Loop filter threads
```

### 2.3. RequestNewFrame Mekanizması

```cpp
// Her RequestNewFrame çağrısı log'lanıyor:
// "RequestNewFrame(reason = %d, file = %s, line = %d)"
//
// Bu, "dirty flag" yaklaşımı değil, "on-demand" render.
// Sadece değişiklik olduğunda yeni frame talep edilir:
// - Kamera hareketi
// - Tile yüklenmesi tamamlandı
// - Animasyon devam ediyor
// - UI değişikliği
```

### 2.4. BuildNextScene Detayı

```cpp
// "BuildNextScene(build_frame = %d)"
// build_frame = monoton artan frame sayacı
//
// Scene build sırası:
// 1. Traversal kamerasını ayarla (SetTraversalCamera)
// 2. Viewport'u ayarla (SetTraversalViewport)
// 3. Quadtree traverse et → visible tile listesi
// 4. Her tile için:
//    a. LOD seviyesi belirle (SSE threshold)
//    b. Raster texture durumunu kontrol et
//    c. Crossfade durumunu hesapla
//    d. Render komutlarını ShaderScene'e ekle
// 5. Label layout hesapla
// 6. Atmosphere/sky parametreleri güncelle
//
// FrameStatusTracker ile scene build durumu takip edilir.
// Eğer build tamamlanamazsa → MissedSceneBuilds++ 
```

---

## 3. Tile Yönetim Sistemi

### 3.1. Tile Veri Tipleri (Asset Types)

WASM string analizinden çıkarılan tam asset hiyerarşisi:

```
AbstractAsset (base)
├── VectorTileAsset           ← Yol, bina, label polygon verileri
├── DiffTileAsset             ← Incremental tile güncellemeleri
├── PhotoTileLoadableAsset    ← Uydu/hava fotoğrafı tile'ları
├── ThreeDTilesSetAsset       ← 3D bina/fotogrametri (glTF)
├── RockNodeSetAsset          ← RockTree düğümleri (terrain mesh)
├── RockMeshAsset             ← RockTree mesh verileri
├── GlobalStyleTableAsset     ← Stil tabloları
├── PaintParametersAsset      ← Paint/render parametreleri
├── ImageAsset                ← Genel resim asset'leri
├── LinkAsset                 ← KML link verileri
├── PhotoMeshAsset            ← Street View mesh
├── PhotoMetadataAsset        ← Street View metadata
├── PhotoQueryAsset           ← Photo sorgu sonuçları
├── MapCopyrightsAsset        ← Harita telif hakları
├── RockCopyrightsAsset       ← RockTree telif hakları
├── ReferenceLoaderAsset      ← Referans yükleyici
├── AreaConnectivityAsset     ← Alan bağlantı verileri
├── KeyedAsset                ← Anahtarlı generic asset
└── DefaultLoadableAsset      ← Varsayılan yüklenebilir
```

### 3.2. Asset Loader Pipeline

```
AssetLoader Pipeline (her asset tipi için):
┌─────────────────────────────────────────────────────┐
│ 1. SCHEDULE                                         │
│    ├── Priority hesapla (kamera mesafesi, SSE)      │
│    └── Fetch queue'ya ekle                          │
├─────────────────────────────────────────────────────┤
│ 2. FETCH (async, worker thread)                     │
│    ├── DoDiskFetch()  ← Disk cache kontrol          │
│    │   └── DiffTileAssetLoader::DoDiskFetch()       │
│    ├── DoDiskStore(d) ← Disk'e kaydet               │
│    └── Network fetch  ← AssetNetLoads++             │
├─────────────────────────────────────────────────────┤
│ 3. DECODE (worker thread)                           │
│    ├── VectorTileAssetLoader::DoMergeAndLoad()      │
│    │   └── "Time spent in ...DoMergeAndLoad = %f"   │
│    ├── DiffTileAssetLoader::DecodeData()            │
│    ├── ThreeDTilesSetLoader::DecodeData()           │
│    ├── RockNodeSetLoader::DecodeData()              │
│    └── RockMeshLoader::DecodeData()                 │
├─────────────────────────────────────────────────────┤
│ 4. MERGE (main thread)                              │
│    ├── VectorTileAssetLoader::FinishMerge()         │
│    │   └── "Time spent in ...FinishMerge"           │
│    └── GPU texture/buffer upload                    │
├─────────────────────────────────────────────────────┤
│ 5. READY → Render'a hazır                           │
│    └── Expiration yönetimi (main thread)            │
│        └── UpdateExpirationsOnMainThread()          │
└─────────────────────────────────────────────────────┘
```

### 3.3. Tile State Machine (Genişletilmiş)

```
UNLOADED ──schedule──→ SCHEDULED
SCHEDULED ──fetch──→ FETCHING
                       ├── disk hit → DECODING
                       └── network  → received → DECODING
DECODING ──decode──→ UPLOADING (main thread FinishMerge)
UPLOADING ──upload──→ READY
SCHEDULED/FETCHING/DECODING/UPLOADING ──cancel──→ CANCELED
CANCELED ──re-enter view / schedule──→ SCHEDULED
READY ──evict──→ UNLOADED (cache release + tile erase)
FETCHING/DECODING/UPLOADING ──error──→ FAILED
FAILED ──retry (max 3)──→ SCHEDULED
```

### 3.4. Tile Unpop (Progressive Loading) Mekanizması

Google Earth'te tile'lar aniden "pop" etmez. Bunun yerine "unpop" geçişi kullanılır:

```cpp
// String referansları:
// "kPhotoTileUnpopping" — photo tile unpop durumu
// "kRockTreeUnpopping"  — rocktree unpop durumu
// "uUnpopBlend"         — unpop blend uniform'u
// "uTexScaleOffsetUnpop" — unpop texture parametreleri
// "uPhotoTileTextureUnpop" — unpop texture
// "blend between PhotoTileTexture and PhotoTileTextureUnpop"
// "/mirth/core/render/UnpopPairTimeFade" — fade süresi config
// "unpop_speed_limit_ndc" — NDC'de hız limiti
// "Unpopping is disabled when IUnpoppable moves faster than this speed in NDC in one frame."

// Unpop Mekanizması:
// 1. Düşük çözünürlük tile hemen gösterilir (parent veya cache)
// 2. Yüksek çözünürlük tile yüklenirken, düşük tile fade-out olur
// 3. Blend factor 0→1 arasında interpolate edilir
// 4. Kamera çok hızlı hareket ederse unpop devre dışı kalır
//    (anlamsız transition'ları önlemek için)
```

### 3.5. Raster Crossfade

```cpp
// Tile geçişlerinde iki raster arasında crossfade:
// "RASTER_CROSSFADE" — shader define
// "uCrossfadeInterpolant" — crossfade interpolant uniform [0,1]
// "Crossfade interpolant in [0,1]." — açıklama string'i
// "SetCrossfade(crossfade = %d)" — API çağrısı
// "SetCrossfade(val = %d)" — değer set
// "GetCrossfade" — değer oku
//
// RASTER_SINGLE_TEXTURE: Tek texture, crossfade yok
// RASTER_CROSSFADE: İki texture arası fade
```

---

## 4. LOD (Level of Detail) Seçim Sistemi

### 4.1. Quadtree Traversal

```cpp
// Mirth engine quadtree traversal sınıfları:
// mirth::tree::TraversalOutput     — traversal sonuç verileri
// mirth::tree::DataNode::TraversalState — düğüm traversal durumu
// mirth::tree::NodeTraversalState  — düğüm görünürlük durumu
// mirth::tree::LodInfo             — LOD bilgisi
// mirth::tree::PathDataNode<geodesy::TriTreePath> — coğrafi path
// mirth::tree::AbstractPathDataNode — abstract path düğümü
// mirth::tree::UniqueNodePool      — düğüm havuzu (bellek optimizasyonu)

// NOT: Google Earth "TriTreePath" kullanıyor!
// Bu, standart QuadTree yerine icosahedral triangle tree olabilir.
// Ama standard Web Mercator tile'lar da destekleniyor (MercTileDatabase).
```

### 4.2. LOD Karar Mekanizması

```cpp
// GPU Shader Inputs (vertex attribute ve uniform'lar):
// aTileCoords    — tile corner koordinatları [0-1]
// aImageCoords   — imagery texture koordinatları
// aQuadCoords    — quad pozisyon koordinatları
// uTileParams    — tile parametreleri (scale, bias, vb.)
// uCornerLods    — "Tile corner lods for bilinear interp."

// uCornerLods mekanizması:
// - Her tile köşesine farklı LOD seviyesi atanır
// - GPU shader'da bilinear interpolation ile smooth LOD geçişi
// - Tile kenarlarında LOD seam'leri önlenir
// - Bu, Google Earth'ün pürüzsüz LOD geçiş sırrı

// LOD Pixel Thresholds:
// maxLodPixels — bu pixel'den büyükse refine et
// minLodPixels — bu pixel'den küçükse görünmez yap
// "GetMaxLodPixels", "GetMinLodPixels" — API'ler

// Geometric Error:
// "geometricError" — glTF/3DTiles standardından
// Screen Space Error = geometricError * focalLength / distance
```

### 4.3. Skirt Generation

```
// "Skirts" — WASM string'i
// "SKIRTS" — shader define
//
// Skirt stratejisi (önceki analizle tutarlı):
// 1. Tile kenar vertex'leri kopyalanır
// 2. Dünya merkezine doğru aşağı itilir (skirt_depth)
// 3. Orijinal kenar ile skirt arası triangle'lar oluşturulur
// 4. Farklı LOD tile'ları arasındaki boşluklar kapatılır
```

---

## 5. DEM / Elevation Sistemi

### 5.1. Elevation Query API'leri

```cpp
// Senkron sorgu:
// "GetTerrainElevation(latitude = %f, longitude = %f, elevation_type = %d)"

// Asenkron yüksek doğruluklu sorgu:
// "GetAccurateTerrainElevation(latitude = %f, longitude = %f, 
//   desired_accuracy_meters = %f, elevation_type = %d, callback = ...)"

// Sorgu sayacı:
// "GetAccurateTerrainElevationQueryCount()"

// İptal:
// "CancelAccurateTerrainElevationQuery(id = %u)"

// Altitude mode dönüşümü:
// "GetElevationTypeFromAltitudeMode(altitude_mode = %d)"

// Raycast:
// "Raycast(world_ray = %p, elevation_type = %d, point_lla = %p)"
```

### 5.2. DEM Veri Pipeline'ı (Detaylı)

```
                    ┌──────────────────────────┐
                    │  Kullanıcı / Sistem       │
                    │  GetTerrainElevation()    │
                    │  GetAccurateTerrainElev() │
                    └──────────┬───────────────┘
                               │
                    ┌──────────▼───────────────┐
                    │  ElevationQueryProcessor  │
                    │  ::ScheduleRequestProcessing │
                    │                           │
                    │  ├── Cache'de var mı?     │
                    │  │   └── Evet → hemen dön │
                    │  └── Hayır → network req  │
                    └──────────┬───────────────┘
                               │
              ┌────────────────▼────────────────┐
              │  RefinedElevationsRequester      │
              │  ::FetchRefinedElevations()      │
              │                                  │
              │  Input: vector<LatitudeLongitude> │
              │  - latitude (double)              │
              │  - longitude (double)             │
              └────────────────┬────────────────┘
                               │
                    ┌──────────▼───────────────┐
                    │  Network Request          │
                    │  google.internal.earth    │
                    │  .v1.terrain              │
                    │  .BatchGetElevationsByPointRequest │
                    │                           │
                    │  Protobuf RPC:            │
                    │  - Batch of LatLon points │
                    │  - Elevation type          │
                    └──────────┬───────────────┘
                               │
                    ┌──────────▼───────────────┐
                    │  Network Response         │
                    │  .BatchGetElevationsByPointResponse │
                    │                           │
                    │  Returns: vector<double>  │
                    │  (elevation values)       │
                    └──────────┬───────────────┘
                               │
              ┌────────────────▼────────────────┐
              │  TransformStatusOrStringFuture   │
              │  ToProto<BatchGetElevations...>  │
              │                                  │
              │  Protobuf deserialize            │
              │  → elevation array               │
              └────────────────┬────────────────┘
                               │
                    ┌──────────▼───────────────┐
                    │  CompletableFuture        │
                    │  → callback ile sonuç     │
                    │  → Cache'e yaz            │
                    │  → Tile mesh güncelle     │
                    └──────────────────────────┘
```

### 5.3. Elevation Tipleri

```cpp
enum ElevationType {
    ELLIPSOID = 0,      // WGS84 ellipsoid (ham)
    TERRAIN = 1,        // Terrain heightmap (DEM)
    SEA_LEVEL = 2,      // Ortalama deniz seviyesi
    BATHYMETRY = 3      // Sualtı terrain
};

enum AltitudeMode {
    CLAMP_TO_GROUND = 0,      // Zemine yapıştır
    RELATIVE_TO_GROUND = 1,   // Zeminden rölatif
    ABSOLUTE = 2,              // Ellipsoid'den mutlak
    CLAMP_TO_SEA_FLOOR = 3,   // Deniz tabanına yapıştır
    RELATIVE_TO_SEA_FLOOR = 4  // Deniz tabanından rölatif
};
```

### 5.4. Ground Elevation Metrikleri

```
// "GROUND_ELEVATION_METRICS_ENABLED" — feature flag
// "ground_elevation_norm" — normalize edilmiş zemin yüksekliği
// "lookatTerrainAlt" — LookAt noktasındaki terrain yüksekliği
// "lookatTerrainLat" — LookAt terrain latitude
// "lookatTerrainLon" — LookAt terrain longitude
// "[terrainEnabled]" — terrain aktif mi
```

### 5.5. DEM → Mesh Entegrasyonu

```
Elevation verisi → Tile mesh'e nasıl uygulanır:

1. BatchGetElevationsByPoint ile sunucudan elevation grid alınır
2. HeightmapTile oluşturulur (width × height float grid)
3. TerrainMeshGenerator::GenerateMesh() çağrılır:
   a. Her grid noktası → LatLonAlt → ECEF dönüşümü
   b. Vertex pozisyonları = ECEF (Earth-Centered Earth-Fixed)
   c. Normal hesaplanır (cross product)
   d. Skirt vertex'leri eklenir
   e. Depth plane equations hesaplanır
      "Plane equations for computing depth of each tile mesh vertex"
      "Plane indices for computing depth of each tile mesh vertex"
4. GPU'ya upload edilir (main thread'de FinishMerge)

// Depth plane shader kullanımı:
// "GetDepthPlaneAtDirectionWorld(...)" — world direction'dan depth plane bul
// GPU shader: depth = dot(vertex.xyz, plane.abc) + plane.d
```

---

## 6. Render Pipeline Detayı

### 6.1. Database Tipleri

```cpp
// Google Earth birden fazla "database" tipi kullanır:
// RasterDatabase         ← Uydu/terrain raster tile'ları
// RasterMapDatabase      ← "Database is not a RasterMapDatabase."
// PaintFeDatabase        ← Paint feature database
// VideoDatabase          ← Video tile database
// RockTreeDatabase       ← 3D mesh database (terrain + buildings)
// MercTileDatabase       ← Mercator projection tile database
//
// Her database kendi tile hiyerarşisini yönetir.
```

### 6.2. Terrain Render Pass

```cpp
// Shader Uniform'ları:
// uTileParams           — tile scale/offset parametreleri
// aTileCoords           — vertex attribute, tile içi koordinat
// uCornerLods           — köşe LOD seviyeleri (bilinear interp.)
// uTexCoordScaleBias    — texture koordinat dönüşümü
// uTexCoordScale_Bias   — alternatif format

// Shader Define'ları (kombinasyonlar):
// RASTER                      — raster mode aktif
// RASTER_SINGLE_TEXTURE       — tek texture
// RASTER_CROSSFADE            — iki texture arası geçiş
// NOTERRAIN_TEXTURE           — terrain texture yok
// ENABLE_TERRAIN_ATMOSPHERE_NOSCATTER — terrain atmosphere (scatter yok)
// ENABLE_ROCK_NORMALS         — rock normal map
// SKIRTS                      — skirt render
// ENABLE_MESH_LIGHTING        — mesh aydınlatma
// ENABLE_BILLBOARD            — billboard render
// ENABLE_BILLBOARD_FIXED      — sabit boyut billboard

// Raster Texture Tanımları:
// "Raster texture."           — ana raster texture
// "Raster fade out texture."  — fade-out transition texture
// "Built entity has no rasters." — raster eksik hatası
```

### 6.3. Photo Tile Render

```cpp
// Photo tile (uydu imagery) render:
// "uPhotoTileTextureUnpop"    — unpop texture sampler
// "uTexScaleOffsetUnpop"      — unpop scale/offset
// "photo tile unpop texture"  — texture açıklaması
// "blend between photo texture and other texture"
// "blend between PhotoTileTexture and PhotoTileTextureUnpop"
// "Parameters to place the photo tile mesh onto the sphere"
//
// Photo tile mesh → sphere'e yerleştirme:
// 1. Photo tile mesh oluştur (düz grid)
// 2. Sphere parametreleri ile küre yüzeyine "drape" et
// 3. Texture koordinatları ile imagery uygula
// 4. Unpop blend ile smooth geçiş
```

### 6.4. Render State Yönetimi

```cpp
// Terrain için tipik render state:
// - Depth test: ON (GL_LEQUAL)
// - Backface cull: ON (arkayı render etme)
// - Blend: OFF (opaque terrain)
// - Polygon offset: ON (z-fighting önleme)
//   "EXT_polygon_offset_clamp" uzantısı destekleniyor
//
// Overlay/label için:
// - Blend: ON (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
// - Depth write: OFF
//
// Water için:
// - Blend: ON (yarı saydam su)
// - ENABLE_IMPROVED_WATER shader
```

### 6.5. Multi-pass Render Sırası

```
Frame Render Order (EarthFrameHandler'dan çıkarılan):
─────────────────────────────────────────────────────
Pass 1: Terrain/Raster base
   ├── Opaque terrain mesh + satellite imagery
   ├── SKIRTS (LOD seam önleme)
   └── Depth buffer yazılır

Pass 2: Water
   ├── ENABLE_IMPROVED_WATER
   ├── ENABLE_WATER_LIGHTING
   └── ENABLE_UNDER_WATER_LIGHTING

Pass 3: 3D Tiles (RockTree/BuiltEnv)
   ├── Photogrammetric buildings
   ├── ThreeDTilesSet (glTF)
   └── Rock mesh (terrain detail)

Pass 4: Vector overlays
   ├── Roads, boundaries
   ├── Polygons (draped on terrain)
   └── Labels (geosurfacelabels)

Pass 5: Atmosphere
   ├── ENABLE_ATMOSPHERE
   ├── Inscatter / aerial perspective
   └── APPROXIMATE_AERIAL_PERSPECTIVE

Pass 6: Sky
   ├── Skybox (ENABLE_SKY_SKYBOX)
   ├── Stars (ENABLE_STAR_SHADER)
   ├── Moon (ENABLE_MOON_SHADER)
   └── Eclipse (ENABLE_ECLIPSE_SHADER)

Pass 7: Clouds
   ├── ENABLE_CLOUD_SHADER
   └── ENABLE_CLOUD_SHADOWS

Pass 8: Post-processing
   ├── City lights (ENABLE_CITY_LIGHT_SHADER)
   ├── Motion blur (ENABLE_MOTION_BLUR)
   ├── Depth of field (ENABLE_DEPTH_OF_FIELD)
   └── Contrast adjustment (ENABLE_ADJUST_CONTRAST)

Pass 9: UI Overlays
   ├── Labels (MULTISAMPLE_LABELS)
   ├── Icons
   └── Navigation controls
```

---

## 7. Threading Modeli

### 7.1. Thread Yapısı

```
Main Thread
├── InstanceImpl::DoFrame()
├── BuildNextScene()
├── VectorTileAssetLoader::FinishMerge() ← GPU upload
├── UpdateExpirationsOnMainThread()
├── SetRenderedStringOnMainThread()
├── Loader::SetValueOnMainThread()
├── ExecuteOnMainThread()
├── OnBaseUrlLoadedMainThread()
└── GL context (SwapBuffers)

DoFrame Thread (DoFrame_thread)
├── InstanceImpl::DoFrameThread()
├── Camera update
├── Scene traversal
└── Tile priority hesaplama

Worker Pool (workerpooljobrunner.cc)
├── VectorTile decode + merge
├── DiffTile decode
├── ThreeDTilesSet decode (glTF parse)
├── RockNode/RockMesh decode
├── Image decode (JPEG, PNG, WebP)
│   ├── vpx tile worker (VP8/VP9)
│   └── Loop filter threads
└── Protobuf deserialize

Tile Decoder Threads
├── "Tile decoder thread creation failed"
├── "Failed to allocate pbi->tile_workers"
├── "Failed to allocate pbi->tile_worker_data"
└── Max worker count = pbi->tile_workers array boyutu
```

### 7.2. Thread Ownership Matrix (Kod-Doğrulamalı)

| Operasyon | Thread | Kaynak |
|-----------|--------|--------|
| Camera update + input apply | Main | `src/engine/globe_engine.cpp` |
| LOD traversal / scene build (BuildNextScene eşdeğeri) | Main | `src/scheduling/tile_pyramid.cpp` |
| Tile fetch | Worker pool (varsayılan 16) | `src/io/tile_fetcher.cpp` |
| Tile decode | Worker pool (varsayılan 8) | `src/io/tile_decoder.cpp` |
| DEM fetch | DEM worker pool | `src/io/dem_manager.cpp` |
| Mesh build | Mesh worker pool (varsayılan 4) | `src/rendering/tile_mesh_scheduler.cpp` |
| Texture upload (`FinishMerge` eşdeğeri) | **Main** | `src/scheduling/tile_scheduler.cpp` + `src/engine/globe_engine.cpp` |
| Mesh GPU upload | **Main** | `src/engine/globe_engine.cpp` |
| Disk cache read/write | Fetch worker | `src/io/tile_fetcher.cpp` |
| Eviction | Main | `src/engine/globe_engine.cpp` |

### 7.3. İş Dispatch Mekanizması

```cpp
// geo/render/mirth/core/base/job/jobdispatcher.cc
// geo/render/mirth/core/base/job/workerpooljobrunner.cc
//
// Job sistemi:
// 1. Job oluştur (callback + priority)
// 2. JobDispatcher'a submit et
// 3. WorkerPoolJobRunner uygun thread'de çalıştır
// 4. Tamamlandığında main thread'e notify et
//
// Thread checker ile thread safety:
// earth::ThreadChecker — hangi thread'de olduğumuzu kontrol
// "Threading::IsMainThread()" — main thread kontrolü
```

### 7.4. Closure / Job Scheduling (WASM String Ground-Truth)

WASM string'leri, `JobDispatcher` tarafında sadece worker işleri değil, **frame-bound closure scheduling** olduğunu da gösteriyor:

```text
"AddClosure(job_type=%d, closure=%s)"
"AddClosure(job_type=%d, closure=%s, delay_by_seconds=%f)"
"AddClosureNextFrame(job_type=%d, closure=%s)"

"AddJob(job_type=%d, apijob=%p)"
"AddJobDelayedBy(job_type=%d, apijob=%p, delay_by_seconds=%f)"
"AddJobNextFrame(job_type=%d, apijob=%p)"
```

Delayed scheduling tarafında ayrıca şu sinyal var:

```text
"JobDispatcher could not instantiate an Alarm."
```

Job örnekleri (string'lerde isim olarak görünen):

```text
"KmlManager::ProcessJob"
"VfsRequestHandler::VfsJob"
"ZipRequestHandler::ZipJob"
"TextureAtlasManager::UpdateAtlasesJob"
"StreetViewImpl::LoadPanoJob"
"VideoJob"
```

**Yorum:** GE runtime'ı pratikte şu modele işaret ediyor:
- `JobDispatcher` ana zamanlayıcı (next-frame + delayed)
- `WorkerPoolJobRunner` heavy decode/merge işleri
- Main thread: GPU upload + `...OnMainThread` proxy işleri

---

## 8. Cache Sistemi

### 8.1. Cache Katmanları

```
Katman 1: GPU Memory (Texture/Buffer cache)
├── TextureAtlasManager
├── RenderContextManager  
└── Aktif tile texture'ları

Katman 2: Memory Cache (decode edilmiş veriler)
├── ClearMemoryCache
├── ReclaimMemoryCache
├── SetMemoryUsageTargetMb
├── GetCurrentMemoryUsageMb
├── GetCurrentMemoryCacheSizeMb
├── GetTargetMemoryCacheSizeMb
└── "Memory limit exceeded (tracked usage %d bytes, limit %d bytes)"

Katman 3: Disk Cache (ham network yanıtları)
├── ClearDiskCache
├── IDiskCache interface
├── DoDiskFetch() — disk'ten oku
└── DoDiskStore(d) — disk'e yaz

Katman 4: Network (sunucu)
├── INetwork interface
├── FetchManager
└── AssetNetLoads counter
```

### 8.2. Cache Eviction (maps_tactile Protobuf'dan)

```protobuf
// WASM'dan çıkarılan cache koşulları:
// maps_tactile.shared.caching.CacheCondition
// ├── Age                    — yaş bazlı expiration
// ├── ViewportOverlap        — viewport ile örtüşme
// │   └── Area (numerator/denominator)
// ├── ViewportZoom            — zoom seviyesi
// ├── UserLocation           — kullanıcı konumu
// ├── TimeOfDay              — günün saati
// └── InAppUserDataModification — uygulama içi değişiklik
//
// CacheDirective:
// ├── ttl_sec                — time-to-live saniye
// ├── display_condition      — gösterim koşulu
// ├── refetch_condition      — yeniden fetch koşulu
// ├── cache_key              — cache anahtarı
// └── disablePersistAcrossAppLaunches — uygulama arası persist
```

### 8.3. Loader Cancelation ve Memory Manager Sinyalleri

WASM string'lerinde iki kritik cancellation/memory kontrol ipucu var:

- Loader cancellation (touch-based): `Number of frames after which a Loader gets canceled if the Asset hasn't been touched.`
- Dedicated memory manager toggle: `/mirth/mirthview/EarthMemoryManagerImpl/earth_memory_manager_enabled`

**Yorum:**
- Loader'lar "touch" (asset son frame'lerde kullanıldı) sinyaliyle yaşatılıyor; belirli frame sayısı boyunca dokunulmazsa iptal ediliyor.
- Mode seviyesinde de fetch iptali var: `/mirth/mode/framework/MirthMode/cancel_old_fetches`.

---

## 9. Anahtar Sabitler ve Yapılandırma

### 9.1. Render Sabitleri (WASM data section'dan)

```
Earth Radius (WGS84):    6,378,137.0 m
Default FOV:             45.0°
Default Tile Size:       256 px
Max Zoom Level:          22
Shared Memory:           512MB initial, 2GB max
Max Retry Count:         3
SSE Threshold:           ~2.0 (standard quality)
```

### 9.2. Shader Attribute İsimleri

```
aTileCoords       — tile köşe koordinatları
aImageCoords      — imagery texture koordinatları  
aQuadCoords       — quad pozisyon
aTexCoords        — genel texture koordinatları
aAntialiasCoords  — anti-alias koordinatları
aVertex           — vertex pozisyon
uCornerLods       — köşe LOD seviyeleri
uTileParams       — tile parametreleri
uCrossfadeInterpolant — crossfade blend
uUnpopBlend       — unpop blend factor
uTexScaleOffsetUnpop — unpop texture transform
uPhotoTileTextureUnpop — unpop texture sampler
uExternalDrapedMercatorParams — draped mercator parametreleri
uGlobalAALodBias  — global anti-alias LOD bias
uViewUpDir        — view up direction
uViewForwardDir   — view forward direction
uSkyboxSunDirAtmo_HeadingBias — skybox/sun parametreleri
uTime             — animasyon zamanı
uDpOffset_Opacity — depth push + opacity
uDepthPush_Opacity — depth push + opacity (alternatif)
uOffsetVec        — offset vector
uNicScaleOffset   — NIC scale/offset
```

### 9.3. Mirth Config Key'leri (String Ground-Truth)

Tile/render/cache/threading davranışını etkileyen bazı config key'leri WASM'da string olarak görünüyor:

```text
/mirth/mode/framework/MirthMode/cancel_old_fetches
/mirth/mode/framework/MirthMode/force_view_changed

/mirth/mirthview/ApiLock/apilock_slow_acquire_lock_warning_ms
/mirth/mirthview/ApiLock/apilock_slow_held_lock_warning_ms
/mirth/mirthview/ApiLock/api_logging

/mirth/earth/EarthFrameHandler/disable_drape_view_updates
/mirth/earth/EarthFrameHandler/disable_drape_texture_updates

/mirth/mirthview/EarthMemoryManagerImpl/earth_memory_manager_enabled
```

---

## 10. SardaGlobe İçin Kritik Çıkarımlar

### 10.1. Hemen Uygulanabilir Mimari Kararlar

| Google Earth Özelliği | SardaGlobe Karşılığı | Öncelik |
|---|---|---|
| `InstanceImpl::DoFrame` → `BuildNextScene` → `Render` | Frame loop'u üçe ayır: Update, SceneBuild, Render | ★★★ |
| `TraversalOutput` + `LodInfo` + `uCornerLods` | Bilinear LOD interpolation ile smooth geçiş | ★★★ |
| `BatchGetElevationsByPoint` (protobuf RPC) | DEM tile batch fetch + cache | ★★★ |
| `Unpop` mekanizması (fade-in tile) | Progressive tile loading with blend | ★★ |
| `RASTER_CROSSFADE` | İki tile arası smooth crossfade | ★★ |
| `SKIRTS` (shader define) | LOD seam prevention skirts | ★★★ |
| `Plane equations for depth` | Per-tile depth plane hesaplama | ★★ |
| `DoFrame_thread` ayrı thread | Scene build'i render'dan ayır | ★★ |
| `RequestNewFrame(reason)` | On-demand rendering (dirty flag yerine) | ★★ |
| `mirth-vfs://` + `zipasset://` + `__asset_manifest__.txt` | Engine-internal asset pack/VFS (ikon, glsl lib, renderassets) | ★★ |
| `cancel_old_fetches` + "Loader gets canceled if Asset hasn't been touched" | Fetch/loader cancel politikası (view-change + touch-based) | ★★ |

### 10.2. DEM Entegrasyon Stratejisi

```
Mevcut SardaGlobe:
- DEM verisi yok veya basit
- Flat tile mesh

Hedef (Google Earth benzeri):
1. Tile yüklenirken elevation grid de fetch et
2. Elevation grid → HeightmapTile struct
3. HeightmapTile → TerrainMesh (ECEF vertex + normal + skirt)
4. Depth plane equations hesapla (GPU depth doğruluğu için)
5. Cache elevation verileri (sık kullanılan bölgeler)
6. Async elevation query API (GetTerrainElevation, GetAccurateTerrainElevation)
7. Altitude mode dönüşümleri (CLAMP, RELATIVE, ABSOLUTE)
```

### 10.3. Frame Loop Refactoring Yol Haritası

```
Mevcut: Monolitik render()
Hedef:  3-aşamalı pipeline

Phase 1: DoFrame()
├── Camera update
├── Input processing
└── Animation update

Phase 2: BuildNextScene()
├── Quadtree traversal
├── LOD selection (SSE)
├── Tile request scheduling
├── Frustum + horizon culling
└── Visible tile list oluşturma

Phase 3: RenderScene()  
├── Terrain pass (raster + DEM mesh)
├── Water pass
├── 3D overlay pass
├── Atmosphere pass
├── Sky pass
├── Label/icon pass
└── Post-process pass
```

---

## 11. WASM Runtime ve JS Köprüsü

### 11.1. WASM Memory Layout
Kaynak: `google_earth/DEEP_REVERSE_ENGINEERING.md`
```
(memory 0) 8192 32768 shared
8192 pages  = 512 MB initial
32768 pages = 2 GB max
```
- **Shared memory + atomics** → multi-thread decode / mesh
- **Imports (örnek):**
  - `__pthread_create_js`
  - `__emscripten_init_main_thread_js`
  - `__emscripten_thread_mailbox_await`
  - `emscripten_gl*` (100+ WebGL wrapper)
- **Exports (örnek):** `hg` (ctors), `kg` (_main), `mg` (_malloc), `ng` (_free)

### 11.2. JS Wrapper / Flutter Köprüsü
Kaynak: `earthplugin_web.js`
- `earthplugin_web.js` WASM bootstrap + PThread setup
- Flutter UI (`main.dart.js`) platform channels ile event/command dispatch
- CanvasKit ile UI → WebGL compositing

**ASM_CONSTS Hooks (JS ↔ WASM):**
- `Module.publish(topic, bytes)` → string + byte payload publish hook
- `Module.onViewportResized(w,h)` → viewport resize event
- `earth-wasm-started` + `lfs-cpp-started` DOM events
- `HaveOffsetConverter()` → `wasmOffsetConverter` global check

### 11.3. PThread/Worker Protokolü
Kaynak: `earthplugin_web.js`
Worker başlatma mesajları:
```
cmd = "load" → wasmMemory + wasmModule init
cmd = "run"  → __emscripten_thread_init + start_routine
cmd = "checkMailbox" → __emscripten_check_mailbox
```
Thread adı: `em-pthread` prefix (ENVIRONMENT_IS_PTHREAD).
Standart Emscripten PThread protokolü.

### 11.4. Label/Text Rendering Pipeline (JS-side Rasterization)
Kaynak: `earthplugin_web.js` (LabelRenderer)
- `MAX_LABEL_RENDERS = 32` → render throttling (requestAnimationFrame)
- Hidden canvas pool (capacity = `512×512` area) + reuse
- Font stack: `"Google Sans", Arial, sans-serif`
- `devicePixelRatio` ile scale + tracking/leading oranları
- **4096 px** üstünde clamp (width/height)
- `Module.SetRenderedString(buffer|canvasId, w, h, lineAdvance, requestId)`
- Texture upload: `gl.activeTexture(TEXTURE7)` + `gl.texSubImage2D(...)` + state restore

> Etiketler GPU'da değil JS canvas üzerinde rasterize edilip texture subimage ile atlasa basılıyor.

### 11.5. WebGL Wrapper Coverage (Emscripten)
- WebGL2'nin neredeyse tüm fonksiyonları mevcut (`glDraw*`, `glTex*`, `glUniform*`, `glVertexAttrib*`, `glTransformFeedback*`)
- EXT/ANGLE varyantları maplenmiş (`glDrawArraysInstancedANGLE`, `glVertexAttribDivisorEXT` vb.)
- Debug vendor/renderer query: `WEBGL_debug_renderer_info`, `UNMASKED_RENDERER_WEBGL`

### 11.6. Build/Version Metadata
Exported WASM string pointer'ları:
- `_kVersionStampBuildChangelistStr`
- `_kVersionStampBuildDateTimePstStr`
- `_kVersionStampBuildToolStr`
- `_kVersionStampBuildIdStr`

### 11.7. VFS / ZipAsset Katmanı (mirth-vfs://, zipasset://)

WASM string'leri, render runtime içinde bir **VFS (virtual filesystem)** ve **zip-asset** katmanı olduğuna işaret ediyor:

```text
"mirth-vfs"
"mirth-vfs://focustarget/focustarget.png"
"Server: mirth-vfs://"
"GetVfs()"

"zipasset://"
"__asset_manifest__.txt"
"__asset_manifest__.txtPK"   // ZIP magic (PK)

"VfsRequestHandler::VfsJob"
"ZipRequestHandler::ZipJob"
"ZipAssetManager*"

"N5mirth3net17VfsRequestHandlerE"
"N5mirth3net17ZipRequestHandlerE"
```

**Yorum (çıkarım):**
- Engine bazı asset'leri VFS üzerinden `mirth-vfs://...` ile resolve ediyor (örn. UI/ikon asset'leri).
- Bazı asset'ler ayrıca zip paket içinde taşınıyor ve `zipasset://` ile resolve ediliyor (`__asset_manifest__.txt` + `PK`).
- `VfsRequestHandler` / `ZipRequestHandler` job'ları, bu resolve/fetch hattının JobDispatcher üzerinden çalıştığını gösteriyor.

---

## 12. Koordinat Sistemleri ve Sabitler

### 12.1. WGS84 Constants
```cpp
constexpr double WGS84_A = 6378137.0;      // Semi-major (m)
constexpr double WGS84_E2 = 0.00669437999; // Eccentricity²
constexpr double EARTH_CIRCUMFERENCE = 40075017.0;  // meters
```

### 12.2. TileBounds Matematik
```
n = 2^level
west  =  x      * 360/n - 180
east  = (x + 1) * 360/n - 180
lat(y) = atan(sinh(y * PI)) * 180/PI
y1 = 1 - 2*y/n    → north = lat(y1)
y2 = 1 - 2*(y+1)/n → south = lat(y2)
```

### 12.3. Normalized Tile Coordinates
```
u,v ∈ [0,1]  // tile içi normalize koordinatlar
lon = west  + u * (east - west)
lat = south + v * (north - south)
```
GPU'da `aTileCoords` / `uTileParams` uniform/attrib'lerine karşılık gelir.

### 12.4. WASM'dan Çıkarılan Sabitler
```cpp
constexpr double EARTH_RADIUS = 6378137.0;      // [binary] meters
constexpr double EARTH_CIRCUMFERENCE = 40075017.0;  // [çıkarım] 2*pi*R
constexpr int TILE_SIZE = 256;                  // [binary]
constexpr int MIN_ZOOM = 0;                     // [çıkarım]
constexpr int MAX_ZOOM = 22;                    // [binary]
constexpr double DEFAULT_FOV = 45.0;            // [binary] degrees
constexpr double DEFAULT_NEAR = 1.0;            // [binary]
constexpr double DEFAULT_FAR = 1000000.0;       // [binary]
constexpr float SSE_THRESHOLD = 2.0f;           // [binary] pixels
constexpr size_t MAX_CACHE_MB = 512;            // [çıkarım] runtime tuning
constexpr size_t TARGET_CACHE_MB = 400;         // [çıkarım] runtime tuning
```

### 12.5. SSE Threshold Değerleri
| Mode | SSE | Açıklama |
|------|-----|----------|
| Quality | 1.0 | Çok tile, yüksek kalite |
| Standard | 2.0 | Dengeli |
| Performance | 4.0 | Az tile, hızlı |

### 12.6. URL Şablonları (Pattern Substitutions)
```
{z} / {level}  → zoom
{x}           → tile x
{y}           → tile y (top-down)
{-y}          → TMS invert y
{quadkey}     → Microsoft QuadKey
{bbox}        → "west,south,east,north"
```

---

## 13. Sınırlar / Çıkarılamayanlar

1. **Tam shader kaynakları** — Sadece pattern'lar çıkarılabildi
2. **Proprietary API endpoint'leri** — Google internal URL'ler
3. **Scheduling heuristic değerleri** — Deneysel tuning gerekir
4. **Optimize edilmiş inline code** — Tam reconstruct zor

### 13.1. Kapsam Kararı: TriTreePath vs MercTreePath

Bu iterasyonda core parity uygulama yolu **MercTreePath/WebMercator** ile sınırlandırıldı.

1. SardaGlobe raster/terrain request hattı (`TileKey`, `TilePyramid`, imagery URL templates) Mercator ekseninde deterministik ve test kapsamı bu hat üzerine kurulu.
2. `TriTreePath` kanıtı binary'de var; ancak kutup bölgelerinde distortion/coverage davranışını parity seviyesinde kapatmak için ayrı traversal, seam ve test gate gerektiriyor.
3. Bu nedenle dual-tree yaklaşımı bu iterasyonda "research backlog" olarak ayrıldı; mevcut hedef cancel lifecycle, co-eviction, parity gate ve adaptif limitleri stabilize etmek.
4. Karar: core parity için mercator-only strateji korunur; dual-tree değerlendirmesi ayrı parity fazında ele alınır.

---

## 14. Binary Ground-Truth Eki (2026-02-06 Doğrulama)

Bu bölüm, önceki analizdeki kritik iddiaları doğrudan binary/WAT/JS wrapper kanıtları ile doğrular.
Odak: **Tile + QuadTree**, **DEM + mesh**, **frame render loop**.

### 14.1. WASM Artefact İncelemesi (Doğrudan)

**Dosyalar:**
- `google_earth/wasm_files/earthplugin_web.wasm` = 19.16 MB
- `google_earth/wasm_files/earthplugin_web.wat` = 175 MB, 6,872,202 satır
- SHA256 (wasm): `8cbfe65025f88dda0bd27f7072657657f710fead7fcc009cbb15ca59134236a7`
- SHA256 (wat): `ac74dc0004d4ce19c0fdb65fd0689bb8379be2987f3cc5e3919078931f866eb9`

**`wasm-objdump -h` kesit özeti:**
```text
Type:      263
Import:    384
Function:  42751
Table:     1
Global:    15
Export:    49
Start:     func 42628
Elem:      1
DataCount: 1707
Code:      42751
Data:      1707
```

**Memory modeli (binary-level):**
```text
memory[0] pages: initial=8192 max=32768 shared <- a.a
```
Bu, JS wrapper'daki `WebAssembly.Memory({initial: ..., maximum: 32768, shared:true})` ile birebir eşleşir.

### 14.2. Tile Yönetimi ve QuadTree/Tree Yapısı (RE Kanıtı)

#### 14.2.1. Tile API yüzeyi
`all_strings.txt` içinde doğrudan:
- `CreateMapTilePyramid(id = %s)`
- `SetMapTilePyramid(val = %p)`
- `GetMapTilePyramid`
- `MapTilePyramidCount`
- `SetTileSize(size = %d)`

Bu, tile sisteminin birincil yapı taşı olarak **MapTilePyramid** kullandığını doğrular.

#### 14.2.2. Quadtree / tree evidence
Doğrudan string/symbol kanıtları:
- `webMercatorQuadtree`
- `N5mirth3map16MercTileDatabaseE`
- `N5mirth4tree15TraversalOutputE`
- `N5mirth4tree7LodInfoE`
- `N5mirth4tree8DataNode14TraversalStateE`
- `N5mirth4tree18NodeTraversalStateE`
- `N5mirth4tree12PathDataTreeINS_7geodesy11TriTreePathEEE`
- `N5mirth4tree8PathNodeINS_7geodesy12MercTreePathENS_3map10VectorNodeEEE`

**Sonuç:** runtime'da tek tip ağaç yok; en az iki path uzayı birlikte kullanılıyor:
1. `TriTreePath` (küresel/earth-rock traversal tarafı)
2. `MercTreePath` + `MercTileDatabase` (Web Mercator tile tarafı)

#### 14.2.3. Tile asset ve loader pipeline
Doğrudan asset string'leri:
- `VectorTileAsset`
- `DiffTileAsset`
- `PhotoTileLoadableAsset`
- `RockNodeSetAsset`
- `RockMeshAsset`
- `ThreeDTilesSetAsset`

Loader/telemetry string'leri:
- `VectorTileAssetLoader::DoMergeAndLoadTime`
- `VectorTileAssetLoader::FinishMergeTime`
- `DiffTileAssetLoader::DecodeDataTime`
- `RockNodeSetLoader::DecodeDataTime`
- `RockMeshLoader::DecodeDataTime`
- `AssetNetLoads`
- `AssetDiskLoads`
- `Tile decoder thread creation failed`

Bu kombinasyon, tile path'inin **fetch → decode → merge/upload** olarak çok-threadli yürüdüğünü doğrular.

### 14.3. DEM (Elevation) Veri Akışı ve Mesh Generation (RE Kanıtı)

#### 14.3.1. DEM API katmanı
String-level doğrulama:
- `GetTerrainElevation(latitude = %f, longitude = %f, elevation_type = %d)`
- `GetAccurateTerrainElevation(latitude = %f, longitude = %f, desired_accuracy_meters = %f, elevation_type = %d, callback = ...)`
- `GetAccurateTerrainElevationQueryCount()`

#### 14.3.2. Network/protobuf zinciri
Doğrudan protobuf tipleri:
- `google.internal.earth.v1.terrain.BatchGetElevationsByPointRequest`
- `google.internal.earth.v1.terrain.BatchGetElevationsByPointResponse`
- `google.internal.earth.v1.LatitudeLongitude`

Doğrudan sınıf/future zinciri:
- `N5earth10elevations26RefinedElevationsRequesterE`
- `RefinedElevationsRequester::FetchRefinedElevations(...)`
- `TransformStatusOrStringFutureToProto<...BatchGetElevationsByPointResponse...>`
- `TransformFuture<...BatchGetElevationsByPointResponse..., vector<double>>`

**Sonuç:** DEM akışı RPC tabanlı batch request ile başlıyor, async `Future` zinciri ile parse edilip `vector<double>` elevation sonuçlarına dönüştürülüyor.

#### 14.3.3. Mesh generation entegrasyonu
Doğrudan mesh/depth ve shader kanıtları:
- `Plane equations for computing depth of each tile mesh vertex`
- `Plane indices for computing depth of each tile mesh vertex`
- `Skirts`
- `uTileParams`
- `aTileCoords`
- `uCornerLods`
- `ground.vp`, `ground.fp`, `rockmesh.vp`, `rockmesh.fp`

**Sonuç:** DEM/terrain sonucu tile mesh'te per-vertex depth plane + corner LOD + skirt kombinasyonu ile render ediliyor.

### 14.4. Frame Render Döngüsü (WASM + JS Köprü Doğrulaması)

#### 14.4.1. Entry ve export mapping
`wasm-objdump` + JS wrapper eşleşmesi:
- export `kg` -> func `32863`
- JS: `var _main = ... wasmExports["kg"]`
- JS `run()` -> `callMain(args)` -> `_main(argc, argv)`

Ayrıca:
- Start section: `func 42628` (runtime init path)
- export `hg` (ctors), `qg` (tls init), thread helpers vb.

#### 14.4.2. Main loop / scheduler köprüsü
JS import map içinde doğrudan:
- `ma:_emscripten_set_main_loop`
- `la:_emscripten_set_main_loop_arg`

Ek olarak JS tarafında:
- `requestAnimationFrame` scheduler yolu
- `MainLoop` zamanlama fonksiyonları

**Sonuç:** outer loop wasm ana fonksiyonu tarafından kurulan Emscripten main loop üzerinden sürülüyor.

#### 14.4.3. Frame pipeline telemetri ve çağrı izleri
Doğrudan stringler:
- `DoFrameCallCount`
- `DoFrame_thread`
- `InterFrameTime`
- `LastDoFrameTime`
- `InstanceImpl::DoFrameThreadTime`
- `InstanceImpl::BuildNextSceneTime`
- `BuildNextScene(build_frame = %d)`
- `RequestNewFrame(reason = %d, file = %s, line = %d)`
- `MissedSceneBuilds`
- `Jank60MissedFrames`
- `Jank30MissedFrames`
- `N5mirth5earth17EarthFrameHandlerE`
- `N5mirth6render11ShaderSceneE`
- `N5mirth18FrameStatusTrackerE`

**Sonuç:** DoFrame ve BuildNextScene ayrımı, request-driven frame tetikleme (`RequestNewFrame`) ve jank ölçümleri binary içinde açıkça mevcut.

#### 14.4.4. Threading kanıtı
JS wrapper worker komutları:
- `cmd==="load"`
- `cmd==="run"`
- `ENVIRONMENT_IS_PTHREAD`

WASM/strings tarafı:
- `Tile decoder thread creation failed`
- `Failed to allocate pbi->tile_workers`
- `Failed to allocate pbi->tile_worker_data`

Bu, render thread + worker decode/toplama modelini doğrular.

### 14.5. Kısa Güven Skoru (Bu Ek İçin)

- **Yüksek güven (doğrudan):** section sayıları, export/import mapping, memory modeli, string/symbol varlığı.
- **Orta güven (çıkarım):** belirli fonksiyonların birebir call order'ı ve bazı internal heuristics (tam decompile olmadan).
- **Düşük güven (bu ekte kullanılmadı):** source-level exact C++ implementation detayları.

---

## Ek: Kaynak Dosya Tam Listesi (mirth engine)

```
geo/render/mirth/mirthview/instanceimpl.cc    ← DoFrame, BuildNextScene, Init
geo/render/mirth/earth/earthframehandler.cc   ← Earth render frame handler
geo/render/mirth/earth/atmosphere.cc          ← Atmosphere render
geo/render/mirth/earth/rocknode.cc            ← RockTree node
geo/render/mirth/earth/rockmeshmanager.cc     ← Rock mesh management
geo/render/mirth/earth/cubemaptexturemanager.cc ← Cubemap textures
geo/render/mirth/earth/exposurecontroller.cc  ← HDR exposure
geo/render/mirth/earth/datedrocknodemanager.cc ← Time-based rock nodes
geo/render/mirth/map/vectortilemanager.cc     ← Vector tile management
geo/render/mirth/map/grayscaleimageutils.cc   ← Grayscale image processing
geo/render/mirth/map/polygonbuilder.cc        ← Polygon geometry builder
geo/render/mirth/core/render/rendercontextmanager.cc  ← Render context
geo/render/mirth/core/render/textureatlasmanager.cc   ← Texture atlas
geo/render/mirth/core/render/model/gltfmodelgeometry.cc ← glTF model
geo/render/mirth/core/render/label/labellayout.cc     ← Label layout
geo/render/mirth/core/render/label/geosurfacelabels.cc ← Geo labels
geo/render/mirth/core/render/video/video.cc           ← Video render
geo/render/mirth/core/render/video/videotexture.cc    ← Video texture
geo/render/mirth/core/render/video/videosync.cc       ← Video sync
geo/render/mirth/core/cache/cachemanager.cc           ← Cache management
geo/render/mirth/core/cache/fetch/fetchmanager.cc     ← Fetch management
geo/render/mirth/core/base/job/jobdispatcher.cc       ← Job dispatch
geo/render/mirth/core/base/job/workerpooljobrunner.cc ← Worker pool
geo/render/mirth/core/base/types/framestatustracker.cc ← Frame status
geo/render/mirth/camera/camerasourcefactoryimpl.cc    ← Camera factory
geo/render/mirth/camera/camerautilsimpl.cc            ← Camera utils
geo/render/mirth/photo/photoframehandler.cc           ← Photo frame
geo/render/mirth/photo/photomeshmanager.cc            ← Photo mesh
geo/render/mirth/photo/fader.cc                       ← Photo fader
geo/render/mirth/solarsystem/solarsystemframehandler.cc ← Solar system
geo/render/mirth/gridlines/gridlinemanagerimpl.cc     ← Grid lines
geo/render/mirth/core/kmlimpl/*.cc                    ← KML rendering
geo/render/mirth/core/kml/rw/kmlversion.cc            ← KML versioning
geo/render/ion/gfx/graphicsmanager.cc                 ← GL management
geo/render/ion/profile/calltracemanager.cc            ← Profiling
geo/render/ion/profile/tracerecorder.cc               ← Trace recording
geo/earth/app/cpp/core/earthcorebase.cc               ← Earth core
geo/earth/app/cpp/core/refinedelevationsrequester/refinedelevationsrequester.cc ← DEM
geo/earth/app/cpp/core/camera/earthrendercamera.cc    ← Earth camera
geo/earth/app/cpp/core/camera/cameramanager.cc        ← Camera management
geo/earth/app/cpp/core/camera/cameraviewobserver.cc   ← Camera observer
geo/earth/app/flutter/plugins/earth/web/earthplugin_web.cc ← WASM entry
geo/earth/app/flutter/plugins/earth/earthplugin.cc    ← Plugin entry
geo/earth/builtenv/lib/preset_utils/raster_geometry.cc ← Raster geometry
geo/earth/builtenv/lib/geometry/geometry_utils.cc     ← Geometry utils
```

---

# BÖLÜM B: 3D Terrain Geliştirme Planı

> **Tarih:** 2026-02-05 (Güncelleme: 2026-02-12)
> **Hedef:** Google Earth benzeri 3D terrain deneyimi (yakın zoom + tilt + orbit)
> **Kapsam:** DEM/elevation pipeline, shader displacement, terrain-aware kamera, tile mesh ve LOD/geçiş kalitesi
> **GE Referans:** BÖLÜM A §5 (DEM Sistemi), §3.4 (Unpop), §4 (LOD), §6.2 (Terrain Render Pass)

### Ana Kural

Bu plan `AGENTS.md` kurallarına bağlıdır:
1. **Tek parity hedefi Google Earth'tür** — tüm davranış, mimari ve UX kararlarında.
2. Mimari iyileştirmeler parity'yi bozmayacak şekilde ilerler.

---

## 15. Mevcut Durum ve Hedef Mimari

### 15.1. Kritik Blocker'lar (Tespit)

1. **DEM kaynağı runtime'da güvenilir değil (401/403 riski):** elevation akışı kırılıyor.
2. **Terrain-aware picking yok:** orbit/pan pivot sphere tabanlı olduğu için GE hissi oluşmuyor.
3. **Displacement authority belirsiz (CPU + GPU birlikte):** çift displacement ve seam riski.
4. **DEM request önceliklendirmesi zayıf:** yakın/ekrandaki tile geç gelebiliyor.
5. **Progressive fallback/morph eksik:** child DEM geç geldiğinde ani "pop" görülüyor.
6. **Terrain/nav parity testleri eksik:** regressions erken yakalanamıyor.

### 15.2. 2026-02-05 Uygulanan Düzeltmeler

1. **DEM kuyruğu FIFO → Priority:** `DemManager` request queue yapısı `priority + score + deterministic sequence` ile güncellendi.
2. **Pending DEM rank-upgrade:** Aynı tile kuyruğa girdikten sonra yüksek öncelik alınca lazy stale-skip ile öne çekilir.
3. **Terrain pick parent fallback:** `PickGlobe` `sampleLevel → ... → 0` parent fallback eklendi.

### 15.3. Hedef Mimari

1. DEM akış sağlığı startup'ta doğrulanmış ve izlenebilir.
2. Tek displacement authority (CPU veya GPU, karışık değil).
3. Kamera pivot/pick terrain-aware.
4. DEM scheduler görünürlük/SSE odaklı.
5. Parent→child progressive terrain geçişi morph ile yumuşatılmış.
6. Otomatik testler ve kabul metrikleriyle parity regressions engellenir.
7. **GE-aligned:** `uCornerLods` bilinear LOD interpolation (§4.2).
8. **GE-aligned:** Unpop/crossfade mekanizması (§3.4, §3.5).
9. **GE-aligned:** Skirt generation (§4.3).
10. **GE-aligned:** Depth plane equations (§5.5).

---

## 15.4. Terrain FAZ Planı

### FAZ 0 — Altyapı Sağlığı ve Telemetri ✅
**Süre:** 1-2 gün | **Öncelik:** P0

- DEM endpoint health-check: startup'ta auth/erişim testi.
- DEM download metriği: success/fail, HTTP code, retry/backoff.
- Failover stratejisi: DEM yoksa açık log + kontrollü fallback.
- Konfigürasyon netliği: DEM URL, header/token, timeout tek yerden.

**DoD:** DEM endpoint erişim hataları log/metrics'te görünür. DEM yokken uygulama stabil.

### FAZ 1 — Elevation Pipeline Stabilizasyonu ✅
**Süre:** 3-4 gün | **Öncelik:** P0

- Displacement authority seçimi: `CPU_MESH_BAKE` / `GPU_HEIGHTMAP_DISPLACE` runtime flag.
- Shader contract: `uHeightScale`, `uHeightMin`, `uHeightMax` etkili + deterministic fallback.
- Edge/seam stratejisi: Komşu LOD geçişleri doğrulanır.
- Debug overlay: Tile'ın elevation alıp almadığı görünür.

**DoD:** Terrain yüksekliği her tile için tek kaynaktan. LOD kenarında belirgin seam yok.

### FAZ 2 — Terrain-Aware Kamera ve Interaksiyon ✅
**Süre:** 3 gün | **Öncelik:** P0

- `TerrainPicker`: Ray → visible terrain tile intersection + parent fallback.
- FlightController: Orbit pivot terrain üzerinde, zoom-to-cursor terrain lock, pan anchor terrain-aware.
- GE parity tuning: Tilt limiti, orbit merkezi, scroll/shift-scroll davranışı.

**DoD:** Cursor altındaki terrain ile orbit pivot uyumlu. Düz sphere pivot sadece fallback modunda.

### FAZ 3 — DEM Scheduler ve Progressive LOD Geçişi ✅
**Süre:** 3-4 gün | **Öncelik:** P1

- SSE/ekran alanı/mesafe bazlı priority queue.
- Kameradan çıkan tile isteklerini düşür.
- Child DEM gelene kadar parent DEM remap + morph (100-250 ms).
- Frame-budgetli upload.
- Cancel lifecycle state machine'e bağlandı (`Canceled` state + untouched cancel policy).

**DoD:** Göze batan pop/çatlak yok. DEM gecikmede parent fallback ile tutarlı yüzey.

### FAZ 4 — Render Parity Core (Corner/Skirt/Depth Kararı)
**Süre:** 3-4 gün | **Öncelik:** P1 | **Durum:** In Progress

1. Unpop/crossfade (`uUnpopBlend`, parent-child blend) aktif ve testli.
2. `uCornerLods` bilinear interpolation aktif; seam görsel gate'i eksik.
3. Skirt generation aktif; selective skirt mask ve stitch mask testleri mevcut.
4. Depth-plane implementasyonu koşullu: önce parity gate ile ölçüm, kritik z-fighting kalırsa depth-plane uygulanır.

**DoD:** Pop/seam kritik olayları görsel gate altında sıfırlanır; depth-plane için ölçüme dayalı go/no-go kararı yazılıdır.

### FAZ 5 — Test, Parity Benchmark ve Release Gate
**Süre:** 2-3 gün | **Öncelik:** P1 | **Durum:** In Progress
**GE Referans:** §3.4 (Unpop), §4.2 (`uCornerLods`), §5.5 (depth precision)

1. DEM fetch/auth/fallback + cancel/re-request regresyon testleri (`TileFetcherCancelRerequestTest`, `TileSchedulerCancelFlowTest`).
2. DEM/raster co-eviction testi (`DemCoEvictionTest`) ve debug metriği (`demCoEvictions`).
3. Terrain-aware pick + continuity testleri (`DemFallbackTest`, `DemContinuityTest`).
4. Dağlık/vadi/kıyı/şehir visual parity benchmark seti.
5. CTest + visual gate kırmızı/yeşil merge kriteri.

**DoD:** Terrain/nav acceptance gate CI'da deterministik çalışır; kritik parity checklist kabul edilir.

---

## 15.5. Terrain Dosya Eşleştirme

| Paket | Ana Dosyalar |
|------|---------------|
| DEM sağlık + telemetri | `src/io/dem_manager.cpp`, `src/core/config.h` |
| Tek displacement authority | `src/rendering/tile_mesh_builder.cpp`, `src/rendering/shader_manager.*`, `src/rendering/tile_renderer.cpp` |
| Terrain picker + kamera | `src/engine/globe_engine.cpp`, `src/camera/flight_controller.cpp` |
| Priority scheduler + progressive | `src/io/dem_manager.*`, `src/scheduling/lod_selector.*`, `src/core/tile.h` |
| Unpop/crossfade | `src/rendering/tile_renderer.cpp`, `src/rendering/shader_manager.cpp`, shader dosyaları |
| Corner LODs + skirts | `src/rendering/tile_mesh_builder.cpp`, `src/core/tile.h`, shader dosyaları |
| Depth planes | `src/rendering/tile_mesh_builder.cpp`, shader dosyaları |

## 15.6. Terrain KPI

1. DEM fetch success rate (görünür tile seti): **≥ %95**
2. Terrain-aware pick başarı oranı: **≥ %99**
3. Yakından tilt/orbit senaryosunda büyük pop/çatlak: **0 kritik olay**
4. Kamera hareketinde FPS düşüşü (terrain aktifken): profil hedefleriyle uyumlu

## 15.7. Terrain Faz Durum Takibi

| Faz | Durum | Not |
|-----|-------|-----|
| FAZ 0 | ✅ Done | DEM health + telemetri (DemStats, CheckHealth, debug panel) |
| FAZ 1 | ✅ Done | Tek displacement authority (DisplacementMode enum, CPU/GPU gate) |
| FAZ 2 | ✅ Done | Terrain-aware picking (iterative DEM refinement in PickGlobe) |
| FAZ 3 | ✅ Done | Priority DEM + progressive fallback + cancel lifecycle entegrasyonu |
| FAZ 4 | 🟡 In Progress | Corner/skirt parity aktif, depth-plane go/no-go ölçümü açık |
| FAZ 5 | 🟡 In Progress | Test + parity gate otomasyonu genişliyor, visual benchmark seti açık |

### Hemen Sonraki İşler
1. Visual parity gate: z-fighting / corner seam / hızlı nav sahneleri için screenshot fark eşiği.
2. Depth-plane go/no-go: log-depth + reversed-Z sonucu kritikleri kapatamıyorsa implementasyonu aç.
3. Adaptive resource limits tuning: gerçek cihaz profillerinde clamp/damping kalibrasyonu.

---

# BÖLÜM C: Tile Pipeline Optimizasyon Planı

> **Versiyon:** 1.2 FINAL ✅
> **Tarih:** 2026-02-05 (Güncelleme: 2026-02-06)
> **Hedef:** Main-thread hitch azaltma, öncelik inversiyonu düzeltme, queue overflow retry döngüsü kaldırma
> **GE Referans:** BÖLÜM A §3.2 (Asset Loader Pipeline), §3.3 (Tile State Machine), §7 (Threading), §8 (Cache)

### GE Pipeline ile Karşılaştırma

| Aşama | Google Earth (§3.2) | SardaGlobe Mevcut | Hedef |
|-------|---------------------|-------------------|-------|
| Schedule | Priority (kamera mesafesi, SSE) | ✅ SSE + ranked required/prefetch | Kalan: threshold tuning |
| Fetch | DoDiskFetch → Network (async) | ✅ Worker fetch + disk/memory cache + cancel hook | Kalan: adaptive limit tuning |
| Decode | Worker pool (DoMergeAndLoad) | ✅ Worker decode + priority/fairness | — |
| Upload | FinishMerge (main thread, budgeted) | ✅ Budget + priority + atlas upload | Kalan: visual gate ile doğrulama |
| Mesh | Worker pool → GPU upload | ✅ `TileMeshScheduler` + frame/time budget | Kalan: adaptif budget tuning |
| Cache | 4-katman (GPU→Memory→Disk→Network) | 🟡 GPU/Decoded/Memory/Disk/Network aktif, promotion tuning kısmi | P6: tuning + invalidation |
| Telemetri | DoFrameCallCount, MissedSceneBuilds, Jank metrics | ✅ Frame/cache/queue/metrikleri aktif | Kalan: parity visual gate metrikleri |
| Unpop | uUnpopBlend + speed limit (§3.4) | ✅ Aktif + testli | — |
| Cancel lifecycle | touch-based `cancel_old_fetches` | 🟡 `Canceled` state + untouched cancel aktif | Kalan: scene bazlı threshold kalibrasyonu |

---

## 16. Pipeline Kısıtları ve Faz Özeti

### 16.1. Kritik Varsayımlar

| Parametre | Varsayılan Değer | Açıklama |
|-----------|------------------|----------|
| `maxConcurrentFetches` | **16** | HTTP worker sayısı |
| `maxConcurrentDecodes` | **8** | Decode worker sayısı |
| `maxInFlightFetches` | **64** | Toplam pending+active fetch limiti |
| `MAX_TEXTURE_UPLOADS_PER_FRAME` | **8** | Frame başına max texture upload |
| `TEXTURE_UPLOAD_BUDGET_MS` | **2.0** | Texture upload time budget (ms) |
| `meshSchedulerWorkers` | **4** | Mesh build worker sayısı |
| `MAX_MESH_REBUILDS_PER_FRAME` | **4** | Frame başına max mesh GPU upload |
| `meshUploadBudgetMs` | **2.0** | Mesh GPU upload time budget (ms) |

**Thread-Safety:** `DemManager::GetHeightSampler()` thread-safe (read-only). Shared EBO ownership `MeshTemplate` sınıfına ait.

### 16.2. Pipeline Akışı

```
LOD Select → Request → Fetch → Decode → Upload → Mesh Build → Render
    ↓           ↓         ↓        ↓         ↓          ↓          ↓
TilePyramid  Scheduler  Fetcher  Decoder  TexManager  MeshScheduler RenderFrame
(main)       (main)     (16 wrk) (8 wrk)  (main+bgt)  (4 wrk+main) (main)
   └──────────── stale/untouched tiles ──Cancel──→ Canceled ──re-enter──→ Scheduled
```

**GE karşılığı (§3.2):** `SCHEDULE → FETCH → DECODE → MERGE(main) → READY`

### 16.3. Faz Tablosu

| Faz | Ad | Durum | Kalan İş |
|-----|----|-------|----------|
| P0 | Telemetri & Görünürlük | ✅ Done | Visual parity gate metriklerine bağlama |
| P1 | Fetch/Cache Hattı | ✅ Done | RTT/FPS adaptif tuning |
| P2 | Priority + Lock Azaltma | ✅ Done | Nadir starvation corner-case gözlemi |
| P3 | Backpressure & Queue | ✅ Done | Saha profillerinde queue clamp tuning |
| P4 | Texture Upload | ✅ Done | Cihaz profiline göre budget tuning |
| P5 | Async Mesh Pipeline | ✅ Done | Mesh/upload budget adaptif kalibrasyon |
| P6 | Co-Eviction + Micro-Opt + Adaptive | 🟡 In Progress | visual gate + adaptive feedback kararlılığı |

**Not:** Pipeline rebaseline sonrası kritik kalanlar `cancel lifecycle tuning`, `DEM/raster co-eviction` gözlemi, `adaptiveResourceLimits` kalibrasyonu.

---

## 16.4. P0 — Telemetri & Görünürlük (Önkoşul)
**GE Referans:** §2.1 (DoFrameCallCount, InterFrameTime, MissedSceneBuilds, Jank metrics)

#### P0.1 Frame Timing Ring Buffer
```cpp
// src/core/frame_time_tracker.h (header-only)
struct FrameTimings {
    double lodSelectMs = 0, requestLoopMs = 0, schedulerUpdateMs = 0;
    double textureUploadMs = 0, meshBuildMs = 0, renderMs = 0, totalMs = 0;
};

class FrameTimeTracker {
    std::array<double, 300> frameTimes_;
    int writeIndex_ = 0;
public:
    void Record(double ms);
    double GetP95() const;
    double GetP99() const;
};
```

#### P0.2 Pipeline Counters
```cpp
struct SchedulerStats {
    std::atomic<int> queueWaitCount{0};
    std::atomic<double> avgFetchDurationMs{0};
    std::atomic<double> avgDecodeDurationMs{0};
    int droppedFetch = 0, droppedDecode = 0;
};
```

#### P0.3 Debug Panel
- p95/p99 frame-time, alt süre breakdown, queue sizes, active fetch count

| Dosya | Değişiklik |
|-------|------------|
| `src/core/frame_time_tracker.h` | `FrameTimings`, `FrameTimeTracker` |
| `src/engine/globe_engine.cpp` | timing |
| `src/scheduling/tile_scheduler.h` | `SchedulerStats` |

---

## 16.5. P1 — Fetch/Cache Hattı
**GE Referans:** §3.2 (DoDiskFetch worker), §8 (Cache katmanları), §12.6 (URL şablonları)

#### P1.1 URL Template Parser (regex → sprintf, %90+ hız)
```cpp
class TileUrlTemplate {
public:
    explicit TileUrlTemplate(const std::string& templateUrl);
    std::string Build(int z, int x, int y) const;
private:
    struct Segment { enum Type { Literal, PlaceholderZ, PlaceholderX, PlaceholderY }; Type type; std::string text; };
    std::vector<Segment> segments_;
};
```

#### P1.2 Disk Cache I/O Worker'a Taşı
```cpp
struct FetchRequest {
    TileKey key; std::string url; Priority priority; float score;
    std::function<bool(const TileKey&, std::vector<uint8_t>&)> tryReadCache;
    std::function<void(const TileKey&, const std::vector<uint8_t>&)> writeCache;
};
// Worker loop'ta: cache check → hit? return : HTTP fetch → cache write
```

#### P1.3 CURL Connection Pooling (Thread-Local)
```cpp
static thread_local CURL* tls_curl_;
// curl_easy_reset() per-request (connection reuse korunur)
// TCP_KEEPALIVE, SSL persistent, header list lifecycle
```
**Kazanım:** %50-70 fetch hızı (connection reuse)

#### P1.4 Cancel Hook (Progress Callback)
```cpp
static thread_local std::optional<TileKey> tls_currentKey_;
// curl XFERINFOFUNCTION → cancelled_ set kontrolü → abort transfer
```

| Dosya | Değişiklik |
|-------|------------|
| `src/io/tile_url_template.{h,cpp}` | Yeni |
| `src/io/download_types.h` | Cache callbacks |
| `src/io/tile_fetcher.cpp` | Cache worker, CURL pooling |

---

## 16.6. P2 — Fetcher/Decoder Priority + Lock Azaltma

#### P2.1 Decoder Priority Queue
```cpp
struct DecodeRequest { TileKey key; std::vector<uint8_t> data; Priority priority; float score; };
// std::priority_queue ile urgent tile'lar prefetch'i bypass eder
```

#### P2.2 Callback Lock-Free Pattern
```cpp
// Lock dışında callback çağrısı (contention düşer)
ResultCallback callbackCopy;
{ std::lock_guard lock(mutex_); callbackCopy = resultCallback_; }
if (callbackCopy) callbackCopy(std::move(result));
```

#### P2.3 Priority Starvation Önleme
```cpp
// Fairness: her 4 urgent sonra 1 normal'a şans ver (URGENT_BATCH_SIZE = 4)
```

---

## 16.7. P3 — Scheduler Backpressure

#### P3.1 Bounded Queue + Condition Variable
```cpp
template<typename T>
class BoundedQueue {
    std::queue<T> queue_; std::mutex mutex_;
    std::condition_variable notFull_, notEmpty_;
    size_t maxSize_; std::atomic<bool> closed_{false};
public:
    void Close();           // Shutdown - tüm thread'leri uyandır
    bool Push(T item);      // closed ise false döner
    bool TryPop(T& item);   // non-blocking
};
```

#### P3.2 In-Flight Limit
```cpp
int inFlight = pendingFetches_.size() + fetcher_->GetActiveCount();
if (inFlight >= config_.maxInFlightFetches && priority != Priority::Urgent) return;
```

---

## 16.8. P4 — Texture Upload Optimizasyonu
**GE Referans:** §3.2 (FinishMerge main thread), §7.1 (GPU upload main thread)

#### P4.1 Upload Priority Queue
```cpp
struct UploadJob { TileKey key; Priority priority; float score; };
// priority_queue ile urgent tile texture'ları öne geçer
```

#### P4.2 Texture Reuse (glTexSubImage2D)
```cpp
if (tile.textureId != 0 && tile.texWidth == tile.pixelWidth) {
    glTexSubImage2D(...);  // Reuse
} else {
    CreateTexture(...);    // New
}
```

#### P4.3 Memory Churn Fix
```cpp
void ClearPixels() { pixels.clear(); /* shrink_to_fit yok - capacity korunur */ }
```

#### P4.4 Deferred Mipmap (Opsiyonel)
İlk frame'de mipmap yok, idle frame'de generate.

---

## 16.9. P5 — Async Mesh Pipeline (KRİTİK)
**GE Referans:** §7.1 (Worker Pool decode + mesh), §7.2 (JobDispatcher + WorkerPoolJobRunner)

**Mevcut Sorun:** `BuildTileMesh()` main-thread'de sync → frame stutter

#### P5.1 TileMeshScheduler Modülü
```cpp
struct MeshRequest {
    TileKey key; Extent extent; uint8_t edgeMask;
    int revision; Priority priority; float score;
};
struct MeshResult {
    TileKey key; int revision;
    std::vector<float> vertices;
    std::vector<unsigned int> indices;  // empty when useSharedEBO
    bool useSharedEBO = true, demUsed, demPending;
};
class TileMeshScheduler {
public:
    explicit TileMeshScheduler(int numWorkers = 4);
    void Request(const MeshRequest& req);
    bool TryGetResult(MeshResult& result);
    void SetDemManager(DemManager* dem);
private:
    void WorkerLoop();
    std::priority_queue<MeshRequest, ...> requestQueue_;
    BoundedQueue<MeshResult> resultQueue_;
    std::vector<std::thread> workers_;
};
```

#### P5.2 Shared Index Buffer (MeshTemplate)
```cpp
class MeshTemplate {
public:
    static MeshTemplate& Get(int segments);
    GLuint GetSharedEBO() const;
    int GetIndexCount() const;
    int GetSkirtIndexCount() const;
};
// Per-tile sadece vertex VBO upload → %40 GPU upload azalır
```

#### P5.3 GlobeEngine Entegrasyonu
```cpp
// meshRevision increment tetikleyicileri:
// 1. Edge mask değiştiğinde
// 2. DEM verisi geldiğinde
// 3. Tile extent/segments değiştiğinde

// Update() içinde:
for (const TileKey& key : selection.leafSet) {
    if (!tile.hasMesh && !tile.meshPending) {
        meshScheduler_->Request({key, tile.extent, tile.edgeMask, tile.meshRevision, Priority::Urgent, tile.importance});
        tile.meshPending = true;
    }
}
// Process mesh results:
MeshResult result;
while (meshScheduler_->TryGetResult(result)) {
    if (tile.meshRevision == result.revision)
        TileMeshBuilder::UploadToGPU(tile, result);
}
```

| Dosya | Değişiklik |
|-------|------------|
| `src/rendering/tile_mesh_scheduler.{h,cpp}` | Yeni |
| `src/rendering/mesh_template.{h,cpp}` | Yeni |
| `src/core/tile.h` | meshPending, meshRevision |
| `src/engine/globe_engine.{h,cpp}` | Async mesh entegrasyonu |

---

## 16.10. P6 — Pin/Eviction & Render Micro-Opt
**GE Referans:** §8.2 (Cache Eviction conditions: age, viewport overlap, zoom)

#### P6.1 Pin Set → Epoch
```cpp
struct Tile { uint32_t pinnedEpoch = 0; };
// TextureManager::PinForFrame() → tile.pinnedEpoch = currentEpoch_
```

#### P6.2 Eviction Budget
```cpp
constexpr int MAX_EVICTS_PER_FRAME = 8;
constexpr double EVICT_TIME_BUDGET_MS = 1.0;
// nth_element → partial_sort (top N oldest) + time budget
```

#### P6.3 Render Micro-Opt
```cpp
// glActiveTexture bir kez (BeginBatch)
// glBindVertexArray(0) sadece EndBatch'te
```

---

## 16.11. Pipeline Test Senaryoları

| Test | Senaryo | Beklenen |
|------|---------|----------|
| T1: Hitch | 30 sn hızlı pan/zoom | p95/p99 frame-time düşmeli |
| T2: Latency | Request→Ready median/90p | Median %30+ düşüş |
| T3: Stress | maxZoom zoom in/out | Queue overflow yok, drop = 0 |
| T4: Correctness | Render gap metrikleri | Artmamalı (gap-free korunur) |

## 16.12. Pipeline Uygulama Sırası

```
P0 (Telemetri) ──┐
                  ├── paralel ──→ P1 (Fetch/Cache) → P2 (Priority)
P5 (Async Mesh) ──┘                                  │
                                                     └→ P3 (Backpressure) → P4 (Texture Upload) → P6 (Adaptive + Micro-Opt)
```

**Bağımlılıklar:** P0 salt ölçüm olduğundan P1 ile paralel başlanır. P5 altyapısı aktif olduğu için P4 ile birlikte budget tuning yapılır. Son adım P6 adaptif limit/kalibrasyondur.

## 16.13. Pipeline Public API Değişiklikleri Özeti

| Modül | Değişiklik |
|-------|------------|
| `FetchRequest` | `tryReadCache`, `writeCache` callbacks |
| `DecodeRequest` | `Priority priority`, `float score` |
| `TextureManager` | `UploadJob` struct (priority/score) |
| `Tile` | `pinnedEpoch`, `meshPending`, `meshRevision`, `texWidth/texHeight` |
| `TileState` / `TileStateMachine` | `Canceled` state + `Event::Cancel` |
| `FetchResult` | `bool canceled` (retry/fail akışından ayrıştırma) |
| `Config` | `cancelAfterFramesUntouched`, `demRasterCoEviction`, `adaptiveResourceLimits` |
| `DemManager` | `UnpinAndEvict(const TileKey&)` |
| **Yeni Modüller** | |
| `TileUrlTemplate` | `src/io/tile_url_template.{h,cpp}` |
| `TileMeshScheduler` | `src/rendering/tile_mesh_scheduler.{h,cpp}` |
| `MeshTemplate` | `src/rendering/mesh_template.{h,cpp}` |
| `BoundedQueue` | `src/core/bounded_queue.h` |
| `FrameTimeTracker` | `src/core/frame_time_tracker.h` |

---

## Referanslar

- `AGENTS.md` — Ana kural seti ve doküman indeksi
- `docs/GOOGLE_EARTH_MOUSE_NAVIGATION_ANALYSIS.md` — GE navigasyon RE
- `docs/MASTER_DEVELOPMENT_PLAN.md` — 7-faz yol haritası
- `google_earth/reconstructed_headers/` — Terrain, tile, rendering system headers
- `google_earth/wasm_files/all_strings.txt` — Extracted WASM strings (165K)
