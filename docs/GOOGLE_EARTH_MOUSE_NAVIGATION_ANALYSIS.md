# Google Earth Mouse Navigation Entegrasyonu - Detaylı Analiz

### Proje Bilgisi
- **Tarih:** 2026-02-01 (Güncelleme: 2026-02-03)
- **Kaynak Analiz:** Google Earth WASM/WAT reverse engineering (v10.96.0.1)
- **Hedef:** native_globe uygulamasına Google Earth tarzı mouse navigasyon
- **RE Kaynaklar:** `earthplugin_web.wasm` (19MB), `earthplugin_web.js` (257KB)

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

---

### Faz Özeti

| Faz | Özellik | Dosya | Satır |
|-----|---------|-------|-------|
| **1** | Pan (Grab Earth) | [flight_controller.cpp](cci:7://file:///Users/adilyoltay/Desktop/sardaglobe/src/flight_controller.cpp:0:0-0:0) | 268-340 |
| **2** | Orbit Mode | [flight_controller.cpp](cci:7://file:///Users/adilyoltay/Desktop/sardaglobe/src/flight_controller.cpp:0:0-0:0) | 343-382 |
| **3** | Zoom to Cursor | [flight_controller.cpp](cci:7://file:///Users/adilyoltay/Desktop/sardaglobe/src/flight_controller.cpp:0:0-0:0) | 390-459, 712-766 |
| **4** | Shift+Scroll Tilt | [flight_controller.cpp](cci:7://file:///Users/adilyoltay/Desktop/sardaglobe/src/flight_controller.cpp:0:0-0:0) | 394-401 |

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
| [flight_controller.h](cci:7://file:///Users/adilyoltay/Desktop/sardaglobe/src/flight_controller.h:0:0-0:0) | 4 yeni member variable |
| [flight_controller.cpp](cci:7://file:///Users/adilyoltay/Desktop/sardaglobe/src/flight_controller.cpp:0:0-0:0) | ~150 satır güncelleme |

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

| Özellik | Önce | Şimdi |
|---------|------|-------|
| Pan (Grab Earth) | ⚠️ Arcball | ✅ Great Circle |
| Orbit Mode | ❌ | ✅ Shift+Drag |
| Zoom to Cursor | ⚠️ Partial | ✅ Point-stable |
| Shift+Scroll Tilt | ❌ | ✅ |
| Momentum/Inertia | ⚠️ Basic | ✅ Friction-based |
| Pivot Target Icon | ❌ | ✅ Constant Pixel Size |

**Entegrasyon tamamlandı.** 🎉

---

## Ek: Google Earth WASM Detaylı Teknik Bulgular

### Kaynak Dosya Yolları (WASM'dan Çıkarılan)

```
geo/earth/app/cpp/core/camera/cameramanager.cc
geo/earth/app/cpp/core/camera/earthrendercamera.cc
geo/earth/app/cpp/core/camera/cameraviewobserver.cc
geo/render/mirth/camera/camerasourcefactoryimpl.cc
geo/render/mirth/camera/camerautilsimpl.cc
geo/render/mirth/camera/cameramanipulators/photocameramanipulatorimpl.cc
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

### Analiz Dosyaları

Detaylı analiz çıktıları:
- **WAT Disassembly:** `/Users/adilyoltay/Desktop/google_earth/analysis/wat/earthplugin_web.wat` (176MB)
- **Decompiled:** `/Users/adilyoltay/Desktop/google_earth/analysis/decompiled/earthplugin_web.dcmp` (57MB, 43,815 fonksiyon)
- **Semboller:** `/Users/adilyoltay/Desktop/google_earth/analysis/symbols/`
- **Analiz Raporu:** `/Users/adilyoltay/Desktop/google_earth/analysis/ANALYSIS_REPORT.md`
