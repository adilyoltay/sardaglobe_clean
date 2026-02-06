# Google Earth Mouse Navigation Entegrasyonu - Detaylı Analiz

### Proje Bilgisi
- **Tarih:** 2026-02-01 (Güncelleme: 2026-02-06, Binary Ground-Truth)
- **Kaynak Analiz:** Google Earth WASM/WAT reverse engineering (v10.96.0.1)
- **Hedef:** native_globe uygulamasına Google Earth tarzı mouse navigasyon
- **RE Kaynaklar:** `earthplugin_web.wasm` (19.16MB, 42,751 internal function + 384 import + 49 export), `earthplugin_web.js` (258KB)
- **Ana Referans:** `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md` (Mirth engine tam analiz)

---

## WASM Reverse Engineering Bulguları

### Tespit Edilen Core Sınıflar

| Sınıf | Namespace | Açıklama |
|-------|-----------|----------|
| `CameraManager` | `earth::camera` | Ana kamera kontrol sınıfı |
| `EarthPanRotateZoomAction` | `mirth::api::camera::impl` | Pan/Rotate/Zoom unified action |
| `OrbitAction` | `mirth::api::camera::impl` | Orbit mode action |
| `FovZoomAction` | `mirth::api::camera::impl` | FOV-based zoom |
| `DampedVelocityAction` | `mirth::api::camera::impl` | Momentum/inertia sistemi |
| `MapCameraManipulator` | `mirth::api::camera` | Kamera manipülasyon handler |

### ThrowAnimation Tipleri (Momentum Sistemi)

Google Earth'ün momentum sistemi `ThrowAnimation` sınıfları ile implemente edilmiş:

```
EarthPanRotateZoomAction::ThrowAnimation      - Base throw animation
EarthPanRotateZoomAction::ArcballThrowAnimation - Arcball rotation momentum  
EarthPanRotateZoomAction::RotationThrowAnimation - Great circle rotation momentum
EarthPanRotateZoomAction::ZoomThrowAnimation   - Zoom momentum
EarthPanRotateZoomAction::LlaThrowAnimation    - Lat/Lon/Alt based momentum
```

### Konfigürasyon Parametreleri

WASM'dan çıkarılan kamera konfigürasyon yolları:
```
/mirth/camera/EarthPanRotateZoomAction/horizon_pan_threshold
/mirth/camera/EarthPanRotateZoomAction/tilt_altitude_adjust_limit
/mirth/camera/OrbitAction/horizon_tilt_threshold
/mirth/camera/MapCameraManipulator/begin_tilt_fade
/mirth/camera/MapCameraManipulator/end_tilt_fade
/mirth/camera/MapCameraManipulator/camera_min_altitude_m
/mirth/camera/MapCameraManipulator/sky_fraction
```

### Input Event Handling

```cpp
// Tespit edilen event sınıfları
earth::PointerEvent        // Mouse/Touch pointer events
earth::KeyboardEvent       // Klavye events
earth::InputEvent          // Generic input
earth::InputEventState     // Input state tracking

mirth::api::event::impl::MouseWheelEvent     // Scroll wheel
mirth::api::event::impl::IMouseEventHandler  // Mouse handler interface
mirth::api::event::impl::ITouchEventHandler  // Touch handler interface
```

### Action Adapter Sistemi

Drag-to-action dönüşümü için adapter sınıfları:
```cpp
DragToHeadingAndTiltAdapter      // Drag → Heading + Tilt değişimi
DragToTransformActionAdapter     // Drag → Transform matrix
DragDeltaActionAdapter           // Delta-based drag handling
RotationFilterActionAdapter      // Rotation filtering
DragThresholdFilterActionAdapter // Threshold-based filtering
```

### LookAt Camera Model

```protobuf
// Tespit edilen camera parametreleri
earth.document.protos.LookAtCamera {
    LookAtCameraType type;  // CAMERA, LOOKAT, LOOKATR
    double latitude;
    double longitude;
    double altitude;
    double heading;
    double tilt;
    double range;
}

earth.document.protos.LookAtCameraOptions {
    // Additional camera options
}
```

### Camera Manager API

```cpp
// Tespit edilen CameraManager metodları
CameraManager::FlyCameraToFeatures()
CameraManager::FlyCameraToEarthView()
CameraManager::FlyCameraToStreetView()
CameraManager::SetCameraTo()
CameraManager::SetCameraToInternal()
CameraManager::OnPanoChanged()
```

### 2026-02-06 Binary Ground-Truth Özeti

Bu başlık, navigasyonla doğrudan ilgili iddiaların binary seviyesinde doğrulanmış halini özetler:

| Konu | Binary/WASM Kanıtı | Navigasyon Etkisi |
|------|---------------------|-------------------|
| Entry path | `export "kg" -> func 32863`, JS wrapper: `_main = wasmExports["kg"]` | Kamera güncellemesi frame ana döngüsüne bağlı |
| Main loop | `ma:_emscripten_set_main_loop`, `la:_emscripten_set_main_loop_arg` importları | Input + kamera + render sürekli döngüde eşzamanlı |
| Frame telemetri | `DoFrameCallCount`, `InterFrameTime`, `LastDoFrameTime`, `InstanceImpl::DoFrameThreadTime`, `InstanceImpl::BuildNextSceneTime` | Navigasyon jitter/jank metrikleri ölçülebilir |
| On-demand frame | `RequestNewFrame(reason = %d, file = %s, line = %d)` | Mouse hareketi/animasyon sırasında frame tetikleme |
| Threading | JS worker `cmd="load"`, `cmd="run"`, `ENVIRONMENT_IS_PTHREAD`; string: `Tile decoder thread creation failed` | Input thread + loader thread ayrımı davranışı etkiler |
| Memory modeli | `memory[0] pages: initial=8192 max=32768 shared` | Pthread + paylaşımlı veri yapıları ile düşük gecikmeli etkileşim |

---

## Mirth Engine Camera Mimarisi (2026-02-06 WASM RE)

> Aşağıdaki bulgular `GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md` çalışmasından elde edilmiştir.

### Kaynak Dosya Yapısı (Mirth Engine)

```
geo/render/mirth/
├── camera/
│   ├── camerasourcefactoryimpl.cc    ← Camera source factory
│   ├── camerautilsimpl.cc            ← Camera utility fonksiyonları
│   └── cameramanipulators/
│       └── photocameramanipulatorimpl.cc ← Photo/StreetView kamera
├── mirthview/
│   ├── instanceimpl.cc               ← DoFrame (kamera update tetikler)
│   └── viewimpl.cc                   ← View yönetimi
└── photo/
    ├── photoframehandler.cc          ← StreetView frame handler
    └── fader.cc                      ← Geçiş animasyonları

geo/earth/app/cpp/core/camera/
├── cameramanager.cc               ← Ana kamera yöneticisi
├── earthrendercamera.cc           ← Render kamera hesabı
└── cameraviewobserver.cc          ← Kamera değişiklik gözlemcisi
```

### Frame Loop İçinde Kamera Güncelleme Akışı

Kamera, frame döngüsünün en başında (DoFrame_thread içinde) güncellenir:

```
InstanceImpl::DoFrame()
└── DoFrame_thread (ayrı thread)
    ├── Camera::Update()
    │   ├── CameraManager update
    │   ├── MapCameraManipulatorHandler input işle
    │   └── SetTraversalCamera() ← Quadtree traversal kamerasi ayarla
    │
    ├── RunLoaders [delayed] ...
    └── FinishMerge() ...
```

**Önemli:** Kamera değişikliği hemen traversal kameraya aktarılır (`SetTraversalCamera`). Bu, tile LOD seçiminin her zaman güncel kamerayla yapıldığını garanti eder.

**Ek binary kanıt (telemetry string'leri):**
```cpp
// "DoFrameCallCount"
// "InterFrameTime"
// "LastDoFrameTime"
// "InstanceImpl::DoFrameThreadTime"
// "InstanceImpl::BuildNextSceneTime"
// "BuildNextScene(build_frame = %d)"
```

### Traversal Uzayları ve Navigasyon Etkisi (Yeni)

WASM symbol/string bulguları iki farklı traversal uzayının birlikte kullanıldığını gösteriyor:

```cpp
// Mercator tile tarafı:
// "webMercatorQuadtree"
// N5mirth3map16MercTileDatabaseE
// N5mirth4tree8PathNodeINS_7geodesy12MercTreePathENS_3map10VectorNodeEEE

// Globe/rock/earth tarafı:
// N5mirth4tree12PathDataTreeINS_7geodesy11TriTreePathEEE
// N5mirth4tree8PathTreeINS_7geodesy11TriTreePathENS0_12PathDataNodeIS3_EEEE
// N5mirth4tree15TraversalOutputE
// N5mirth4tree7LodInfoE
```

**Navigasyon yorumu:** Mouse pick/orbit hedefi küresel tarafta (`TriTreePath`) hesaplanırken, harita tabanlı layer'lar Mercator uzayında (`MercTreePath`) kalabiliyor. Bu yüzden GE hissi için pivot seçimi terrain/globe koordinatında tutulmalı, layer pick sonuçları doğrudan pivot'a dönüştürülmemeli.

### RequestNewFrame Mekanizması

Google Earth "dirty flag" değil, **on-demand render** kullanır:

```cpp
// "RequestNewFrame(reason = %d, file = %s, line = %d)"
// Sadece değişiklik olduğunda yeni frame talep edilir:
// - Kamera hareketi
// - Tile yüklenmesi tamamlandı
// - Animasyon devam ediyor
// - UI değişikliği
```

Bu, idle durumda GPU yükünü sıfırlar.

### Terrain-Aware Navigasyon (Raycast & Elevation)

GE'de navigasyon terrain-aware çalışır. Orbit pivot, zoom target ve pan anchor terrain yüksekliğini kullanır:

```cpp
// Raycast API (WASM string'lerinden):
// "Raycast(world_ray = %p, elevation_type = %d, point_lla = %p)"
//
// Senkron elevation sorgusu:
// "GetTerrainElevation(latitude = %f, longitude = %f, elevation_type = %d)"
//
// Asenkron yüksek doğruluklu sorgu:
// "GetAccurateTerrainElevation(latitude = %f, longitude = %f,
//    desired_accuracy_meters = %f, elevation_type = %d, callback = ...)"
//
// Sorgu iptali:
// "CancelAccurateTerrainElevationQuery(id = %u)"
```

**Yeni doğrulanan DEM zinciri (protobuf/future):**
```cpp
// N5earth10elevations26RefinedElevationsRequesterE
// RefinedElevationsRequester::FetchRefinedElevations(...)
// google.internal.earth.v1.terrain.BatchGetElevationsByPointRequest
// google.internal.earth.v1.terrain.BatchGetElevationsByPointResponse
// google.internal.earth.v1.LatitudeLongitude
```

Bu zincir, navigasyon sırasında kullanılan terrain yüksekliğinin tek-point değil batch/refined akıştan geldiğini gösterir; dolayısıyla orbit pivot kararlılığı için `GetTerrainElevation` fallback'i yanında async refined sonucu geldiğinde pivotu yumuşak düzeltmek gerekir.

**Ground Elevation Metrikleri:**
```
GROUND_ELEVATION_METRICS_ENABLED   ← feature flag
lookatTerrainAlt                   ← LookAt noktasındaki terrain yüksekliği
lookatTerrainLat                   ← LookAt terrain latitude
lookatTerrainLon                   ← LookAt terrain longitude
[terrainEnabled]                   ← terrain aktif mi
```

> Bu metrikler kamera davranışını terrain'e bağlar: orbit pivot'u, min altitude'u ve tilt limitini terrain yüksekliğine göre ayarlar.

### Camera Source Tipleri (FlyTo Animasyonları)

WASM'dan çıkarılan tüm kamera source'ları (animasyon türleri):

| Source | Açıklama | Kullanım |
|---|---|---|
| `FiniteCameraSource` | Süreli animasyon (A→B) | FlyTo |
| `BlendCameraSource` | İki kamera kaynağı blend | Geçişler |
| `LinearFlyCameraSource` | Doğrusal uçuş | Yakın FlyTo |
| `ParabolicFlyCameraSource` | Parabolik yay uçuşu | Uzak FlyTo |
| `BalloonFlyCameraSource` | Balon stili yükselme + uçuş | Feature FlyTo |
| `PoiOrbitCameraSource` | POI etrafında orbit | 3D bina orbit |
| `PlanetOrbitCameraSource` | Gezegen orbit | Globe orbit |

### Camera Interpolasyon Sistemi (Genişletilmiş)

```
mirth::SplineInterpolator<GeoCameraParameters>    ← Geo-parametrik spline
mirth::SplineInterpolator<GeoLookAtParameters>    ← LookAt-parametrik spline
mirth::kmlimpl::CameraLinearInterpolator           ← Doğrusal interpolasyon
mirth::kmlimpl::CameraSplineInterpolator           ← Spline interpolasyon
mirth::kmlimpl::CameraBounceInterpolator           ← Bounce efekti (zoom-out-in)
```

**Bounce interpolator** özellikle uzak FlyTo'larda "yüksel → uç → in" efekti verir.

---

### Faz Özeti

| Faz | Özellik | Dosya |
|-----|---------|-------|
| **1** | Pan (Grab Earth) | `src/camera/flight_controller.cpp` |
| **2** | Orbit Mode | `src/camera/flight_controller.cpp` |
| **3** | Zoom to Cursor | `src/camera/flight_controller.cpp` |
| **4** | Shift+Scroll Tilt | `src/camera/flight_controller.cpp` |

---

### Faz 1: Pan (Grab Earth) ✅

**Google Earth İç Yapısı (RE Bulguları):**
```cpp
// mirth::api::camera::impl::EarthPanRotateZoomAction
// Kaynak: geo/earth/app/cpp/core/camera/cameramanager.cc

// İki farklı throw animation tipi kullanılıyor:
// 1. RotationThrowAnimation - Great circle rotation için
// 2. ArcballThrowAnimation  - Arcball rotation için (fallback)

// Konfigürasyon parametresi:
// /mirth/camera/EarthPanRotateZoomAction/horizon_pan_threshold
```

**Algoritma:** Great Circle Rotation
```
1. Mouse down → Anchor point = globe üzerinde tıklanan nokta
2. Mouse move → Current point = mouse altındaki yeni nokta
3. Rotation axis = cross(anchor, current)
4. Rotation angle = acos(dot(anchor, current))
5. Kamerayı bu açıyla döndür → Anchor mouse altında SABİT kalır
```

**Google Earth Momentum Sistemi:**
```cpp
// DampedVelocityAction sınıfı momentum kontrolü sağlıyor
// ThrowAnimation bırakma sonrası animasyonu yönetiyor
// LlaThrowAnimation - Lat/Lon/Alt bazlı momentum (tercih edilen)
```

**Eklenen Özellikler:**
- `m_anchorRadius` - Terrain yüksekliğini koruma
- `m_lastDt` - Doğru momentum hesabı
- Off-globe (gökyüzü) sürükleme desteği
- Friction-based momentum (bırakınca kayma)

---

### Faz 2: Orbit Mode (Shift+Drag) ✅

**Google Earth İç Yapısı (RE Bulguları):**
```cpp
// mirth::api::camera::impl::OrbitAction
// Konfigürasyon: /mirth/camera/OrbitAction/horizon_tilt_threshold

// Adapter sınıfları:
// DragToHeadingAndTiltAdapter - Drag → Heading + Tilt dönüşümü
// RotateTiltAnimation - Tilt animasyonu
// RotateHeadingAnimation - Heading animasyonu

// earth.earthrender.OrbitLocation - Orbit merkez noktası
// earth.compass.SetHeading - Heading güncelleme
// earth.compass.SetTilt - Tilt güncelleme
```

**Algoritma:** Pivot-centered rotation
```
1. Shift+Left click → Pivot point = tıklanan nokta
2. Mouse move → Heading (yatay) + Tilt (dikey) değişimi
3. Kamera pivot etrafında döner, pivot'a bakış korunur
```

**Google Earth Tilt Konfigürasyonu:**
```cpp
// MapCameraManipulator tilt fade parametreleri
/mirth/camera/MapCameraManipulator/begin_tilt_fade
/mirth/camera/MapCameraManipulator/end_tilt_fade
// Altitude'a bağlı tilt sınırlaması
/mirth/camera/EarthPanRotateZoomAction/tilt_altitude_adjust_limit
```

**Eklenen Özellikler:**
- No-pivot fallback: Gökyüzüne tıklandığında screen center pivot olur
- Momentum desteği (bırakınca devam)
- Orta tuş ile aynı davranış

---

### Faz 3: Zoom to Cursor ✅

**Google Earth İç Yapısı (RE Bulguları):**
```cpp
// mirth::api::camera::impl::FovZoomAction - FOV tabanlı zoom
// mirth::api::camera::impl::FovZoomAnimation - Zoom animasyonu
// EarthPanRotateZoomAction::ZoomThrowAnimation - Zoom momentum

// Zoom butonları için:
// earth.zoombuttons.ZoomButtonsViewModelCommand
// earth.zoombuttons.ZoomButtonsViewModelState

// Minimum altitude sınırlaması:
/mirth/camera/MapCameraManipulator/camera_min_altitude_m
```

**Algoritma:** Point-stable zoom
```
Zoom IN:
  1. Cursor altındaki globe noktasını kaydet
  2. Kamerayı bu noktaya doğru hareket ettir
  3. Nokta ekranda SABİT kalır

Zoom OUT:
  1. Kamerayı forward yönünde uzaklaştır
```

**Google Earth Zoom Özellikleri:**
```cpp
// FOV-based zoom vs Distance-based zoom
// FovZoomAction - FOV değiştirerek zoom (alternatif)
// ZoomThrowAnimation - Scroll bırakıldığında momentum
```

**Eklenen Özellikler:**
- `m_zoomPointOnGlobe` - Cursor altındaki nokta
- %20 zoom factor per notch
- Smooth momentum decay

---

### Faz 4: Shift+Scroll Tilt ✅

**Google Earth İç Yapısı (RE Bulguları):**
```cpp
// Tilt kontrolü için tespit edilen sınıflar:
// mirth::api::camera::impl::RotateTiltAnimation
// earth.compass.SetTilt

// Tilt fade konfigürasyonu (altitude'a bağlı):
/mirth/camera/MapCameraManipulator/begin_tilt_fade  // Tilt fade başlangıç altitude
/mirth/camera/MapCameraManipulator/end_tilt_fade    // Tilt fade bitiş altitude

// Altitude-based tilt limiti:
/mirth/camera/EarthPanRotateZoomAction/tilt_altitude_adjust_limit
```

**Algoritma:** Direct tilt modification
```
Shift + Scroll Up   → Tilt artır (daha yatay bakış)
Shift + Scroll Down → Tilt azalt (daha dikey bakış)
Limit: 0.05° - 80°
```

**Sensitivity:** 5° per scroll notch

---

### Pivot Target Icon (Görsel Geribildirim) ✅

Google Earth'te navigasyon (Orbit/Tilt) başladığında, pivot noktasında beliren geçici hedef ikonu implemente edildi.

**Özellikler:**
- **Dinamik Ölçekleme:** İkon boyutu, kamera mesafesine ve FOV'a göre otomatik ayarlanarak ekranda sabit piksel boyutunda (~40px) kalması sağlandı.
- **Otomatik Temizleme:** `OnMouseUp` veya `Shift` tuşunun bırakılması durumunda ikon anında kaldırılır.
- **Yüzey Uyumu:** İkon, pivot noktasındaki yüzey normaline (Up vektörü) göre hizalanarak zemine paralel bir "target" hissi verir.

---

### Değiştirilen Dosyalar

| Dosya | Eklenen/Değiştirilen |
|-------|-----------------------|
| `src/camera/flight_controller.h` | 4 yeni member variable |
| `src/camera/flight_controller.cpp` | ~150 satır güncelleme |

**Header Değişiklikleri:**
```cpp
double m_anchorRadius = 6378.137;    // Faz 1
double m_lastDt = 0.016;             // Faz 1
glm::dvec3 m_zoomPointOnGlobe{0.0};  // Faz 3
bool m_hasZoomPoint = false;          // Faz 3
```

---

### Final Mouse Mapping

| Input | Davranış | Faz |
|-------|----------|-----|
| **Sol sürükle** | Grab Earth - globe mouse'u takip eder | 1 |
| **Shift + Sol sürükle** | Pivot etrafında orbit | 2 |
| **Orta sürükle** | Tilt + Heading değiştir | 2 |
| **Sağ sürükle** | Zoom in/out | - |
| **Scroll** | Cursor'a doğru zoom | 3 |
| **Shift + Scroll** | Tilt değiştir | 4 |
| **Çift tık** | Fly to + 4x zoom | - |

---

### Google Earth Parity Durumu

| Özellik | Önce | Şimdi | GE Referans |
|---------|------|-------|-------------|
| Pan (Grab Earth) | ⚠️ Arcball | ✅ Great Circle | `RotationThrowAnimation` |
| Orbit Mode | ❌ | ✅ Shift+Drag | `OrbitAction` |
| Zoom to Cursor | ⚠️ Partial | ✅ Point-stable | `FovZoomAction` |
| Shift+Scroll Tilt | ❌ | ✅ | `RotateTiltAnimation` |
| Momentum/Inertia | ⚠️ Basic | ✅ Friction-based | `DampedVelocityAction` |
| Pivot Target Icon | ❌ | ✅ Constant Pixel Size | `OrbitLocation` |
| Terrain-Aware Pick | ⚠️ Sphere | ⚠️ Parent fallback | `Raycast()` + `GetTerrainElevation()` |
| On-Demand Render | ❌ | ❌ | `RequestNewFrame()` |
| FlyTo Bounce | ⚠️ Linear | ⚠️ Linear | `ParabolicFlyCameraSource` |

**Temel navigasyon entegrasyonu tamamlandı.** 🎉  
**Sonraki adımlar:** Terrain-aware picking iyileştirme, on-demand render, FlyTo parabolik animasyon.

---

## Ek: Google Earth WASM Detaylı Teknik Bulgular

### Kaynak Dosya Yolları (WASM'dan Çıkarılan — Tam Liste)

```
# Earth core camera
geo/earth/app/cpp/core/camera/cameramanager.cc
geo/earth/app/cpp/core/camera/earthrendercamera.cc
geo/earth/app/cpp/core/camera/cameraviewobserver.cc

# Mirth engine camera
geo/render/mirth/camera/camerasourcefactoryimpl.cc
geo/render/mirth/camera/camerautilsimpl.cc
geo/render/mirth/camera/cameramanipulators/photocameramanipulatorimpl.cc

# Frame loop (kamera tetikleyen)
geo/render/mirth/mirthview/instanceimpl.cc       ← DoFrame, BuildNextScene
geo/render/mirth/mirthview/viewimpl.cc            ← View yönetimi

# Elevation (terrain-aware nav için)
geo/earth/app/cpp/core/refinedelevationsrequester/refinedelevationsrequester.cc
```

### Tam Camera Sınıf Hiyerarşisi

```
mirth::api::camera::
├── ICameraManager
├── ICameraManipulator
│   ├── MapCameraManipulator
│   │   └── MapCameraManipulatorImpl
│   └── PhotoCameraManipulator
│       └── PhotoCameraManipulatorImpl
├── ICameraSource
│   ├── FiniteCameraSource
│   ├── BlendCameraSource
│   ├── LinearFlyCameraSource
│   ├── ParabolicFlyCameraSource
│   ├── BalloonFlyCameraSource
│   ├── PoiOrbitCameraSource
│   └── PlanetOrbitCameraSource
├── IAnimation
│   ├── FiniteAnimation
│   └── InfiniteAnimation
└── impl::
    ├── IAction
    ├── BaseCameraAction
    ├── EarthPanRotateZoomAction
    │   ├── ThrowAnimation
    │   ├── ArcballThrowAnimation
    │   ├── RotationThrowAnimation
    │   ├── ZoomThrowAnimation
    │   └── LlaThrowAnimation
    ├── OrbitAction
    ├── FovZoomAction
    ├── LookAroundAction
    ├── ElevatorAction
    ├── DampedVelocityAction
    └── SpacePanRotateZoomAction
```

### Event Handler Sınıfları

```cpp
// Input event hierarchy
earth::InputEvent
earth::InputEventState
earth::InputEventHandler
earth::PointerEvent
earth::KeyboardEvent
earth::TouchPointer
earth::ResizeEvent

// Mirth event handlers
mirth::api::event::impl::IMouseEventHandler
mirth::api::event::impl::ITouchEventHandler
mirth::api::event::impl::IKeyboardEventHandler
mirth::api::event::impl::MouseWheelEvent

// Bridge handlers (modular architecture)
mirth::api::event::modular::IMouseEventObserver
mirth::api::event::modular::ITouchEventObserver
mirth::api::event::modular::IKeyboardEventObserver
```

### Kamera Interpolasyon Sistemi

```cpp
// Spline interpolation for smooth camera movement
mirth::SplineInterpolator<mirth::view::GeoCameraParameters>
mirth::SplineInterpolator<mirth::view::GeoLookAtParameters>

// Camera interpolators
mirth::view::CameraInterpolator
mirth::kmlimpl::ICameraInterpolator
mirth::kmlimpl::CameraLinearInterpolator
mirth::kmlimpl::CameraSplineInterpolator
mirth::kmlimpl::CameraBounceInterpolator
```

### Pick/Ray Casting Sistemi

```cpp
// Pick handlers for mouse interaction
LayerPickHandler::OnMousePick
PointPickHandler::OnHover
PointPickHandler::UpdatePhantomPointPositionToLookatTarget
SelectionPickHandler::OnFieldChanged
LineStringPickHandler::OnFieldChanged
AreaPickHandler::HandleAreaPick
AreaPickHandler::SetIsHovering
InfoPickManager::SetStyleMode
PickManager::Pick
```

### Protobuf Camera Message Tipleri

```protobuf
earth.document.protos.LookAtCamera
earth.document.protos.LookAtCameraOptions
earth.document.protos.InvalidTimestampCamera
earth.featureupdater.LookAtCamera
earth.featureupdater.LookAtCameraUpdate
earth.featureupdater.LookAtCameraUpdate.Mask
geo.earth.proto.FlyToCamera.LookAt
geo.earth.proto.FlyToCamera.LookFrom
```

### StreetView/Pano Navigation

```cpp
// StreetView specific navigation
earth.streetview.StartPanView
earth.streetview.UpdatePanViewSpeed
earth.streetview.NavigateToNeighborPano
StreetViewViewModel::PanAnimation::PanViewInOneFrame
StreetViewImpl::LoadPanoJob
```

### Analiz Dosyaları ve Referanslar

**Proje içi:**
- `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md` — **Ana GE referans** (tile/DEM/render/mirth engine, 2026-02-06)
- `google_earth/reconstructed_headers/camera_view.h` — Camera, PerspectiveCamera, View, PrefetchView
- `google_earth/reconstructed_headers/rendering_system.h` — Renderer, Shader, RenderState
- `google_earth/DEEP_REVERSE_ENGINEERING.md` — WASM binary analizi

**Ham analiz çıktıları:**
- `google_earth/wasm_files/earthplugin_web.wasm` — Binary (19.16MB)
- `google_earth/wasm_files/earthplugin_web.wat` — WAT disassembly (175MB)
- `google_earth/wasm_files/all_strings.txt` — Extracted strings (165K)
