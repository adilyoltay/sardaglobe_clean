# WebKüre (CAS Web) API Dokümantasyonu

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Pirireis Bilişim tarafından geliştirilen CAS Web, web ortamında üç boyutlu küre ve iki boyutlu düzlem üzerinde çalışan coğrafi analiz sistemidir.

## Modüler Dokümantasyon Yapısı

Detaylı dokümantasyon için aşağıdaki klasörlerdeki MD dosyalarına bakınız:

| Klasör | Açıklama | Dosyalar |
|--------|----------|----------|
| `introduction/` | Kurulum ve Globe parametreleri | `01_kurulum.md`, `02_globe_parametreleri.md`, `03_globe_komutlari.md` |
| `navigation/` | Kamera ve navigasyon işlemleri | `01_tanim.md` - `11_ornekler.md` |
| `vectorLayer/` | Vektör katmanları ve stiller | `01_tanim.md` - `12_askeri_semboller.md` |
| `coordinates/` | Koordinat sistemleri | `01_tanim.md`, `02_metodlar.md`, `03_koordinat_donusumleri.md` |
| `analysis/` | Analiz işlemleri | `01_gorus_analizi.md` - `04_gunes_ay_isiklandirma.md` |
| `objectArray/` | Object Array yapısı | `01_tanim.md`, `02_metodlar.md`, `03_nesne_ekleme.md` |
| `heatmap/` | Isı haritaları | `01_tanim.md`, `02_rasterize_bazli.md`, `03_shader_bazli.md` |
| `drawOrder/` | Çizim sırası | `01_tanim.md`, `02_metodlar.md`, `03_ornekler.md` |
| `math/` | Matematik kütüphanesi | `01_tanim.md`, `02_metodlar.md`, `03_ornekler.md` |
| `language/` | Dil ayarları | `01_dil_ayarlari.md` |
| `errorlog/` | Hata kayıtları | `01_hata_kayitlari.md` |

---

## Kurulum

```bash
npm i @pirireis/webglobe
```

### Proje Yapısı

```
./src
├── index.html
└── node-modules/
    └── @pirireis/webglobe/
        └── webglobe.js  (legacy: main.js)

./anotherdirectory/
├── webglobeserver.dll
└── CSHostID.csytk
```

### Gerekli Server Bileşenleri

- Mesh adresleri için Globe server yüklü olmalıdır
- Raster adresleri için WMS servisi gereklidir
- Cross-domain izinlerini kontrol ediniz
- `webglobeserver.dll` - GlobeApi modülüne ulaşmak için gerekli dll dosyası
- `CSHostID.csytk` - GlobeApi modülü için gerekli izin dosyası

---

## Globe Başlatma

### CSGlobe Parametreleri

```javascript
const globeParameters = {
  canvas: document.getElementById("globe"),
  globeMaxLodLevel: 19,
  geometry: GlobeApi.CSGeometryTypes.GLOBE, // veya PLANE
  raster: {
    url: "http://sampledomain/wms",
    type: GlobeApi.CSRasterTypes.WMS,
    maxLodLevel: 19,
    opacity: 1.0,
    bbox: [-180, -90, 180, 90]
  },
  mesh: {
    url: "http://sampledomain/csglobeMesh",
    type: GlobeApi.CSMeshTypes.WGS84
  },
  globedll: "http://sampledomain/webglobeserver.dll/"
}

const myGlobe = new GlobeApi.CSGlobe(globeParameters)
```

### Geometri Tipleri

| Tip | Açıklama |
|-----|----------|
| `CSGeometryTypes.GLOBE` | 3 boyutlu küre görünümü |
| `CSGeometryTypes.PLANE` | 2 boyutlu düzlem görünümü |

### Raster Tipleri

| Tip | Açıklama |
|-----|----------|
| `CSRasterTypes.XYZ` | XYZ tile servisi |
| `CSRasterTypes.WMTS` | WMTS servisi |
| `CSRasterTypes.WMS` | WMS servisi |
| `CSRasterTypes.TMS` | TMS servisi |

---

## Navigasyon

### Kamera Animasyon İşlemleri

| Metod | Açıklama |
|-------|----------|
| `api_FlyToRegion(long1, lat1, long2, lat2, scale)` | Bölgeye animasyonlu git |
| `api_FlyToRegionDirect(long1, lat1, long2, lat2, scale)` | Bölgeye anında git |
| `api_FlyToPoint(long, lat, dist, northAng, tiltAng)` | Noktaya animasyonlu git |
| `api_FlyToPointDirect(long, lat, dist, northAng, tiltAng)` | Noktaya anında git |
| `api_ZoomToPaperScale(scale)` | Yörünge noktasına git |
| `api_SetContinuousRotation(boolean)` | Devamlı animasyon ayarı |

### Kamera Kontrol İşlemleri

| Metod | Açıklama |
|-------|----------|
| `api_SetCameraCallBack(callbackObj)` | Kamera callback ayarı |
| `api_SetCameraPos(long, lat, dist, northAng, tiltAng, rollAng)` | Kamera pozisyonu güncelle |
| `api_LeaveCamera(externalDistMeter)` | Kamera yönetimini Globe'a aktar |
| `api_GetCurrentLookInfo()` | Kamera bilgilerini al |
| `api_GetDirectPosNatural()` | Kamera bakış bilgilerini al |
| `api_SetDirectPosNatural(naturalpos)` | Kamera bakış bilgilerini ayarla |
| `api_ZoomToLOD(zoomtype)` | LOD seviyesine yaklaş/uzaklaş |

### Kamera Bilgileri

```javascript
const cameraInfo = myGlobe.api_GetCurrentLookInfo()
// Dönen değerler: CenterLong, CenterLat, Distance, NorthAng, Tilt

myGlobe.api_FlyToPoint(
  cameraInfo.OrbitLong,
  cameraInfo.OrbitLat,
  cameraInfo.OrbitDist,
  cameraInfo.NorthAng,
  cameraInfo.Tilt
)
```

---

## Raster Katmanları

### Metodlar

| Metod | Açıklama |
|-------|----------|
| `api_AddRaster(rasterObj, beforeObject)` | Raster ekle |
| `api_SetRasterService(index, rasterObj, clearBBOX)` | Raster güncelle |
| `api_DeleteRaster(index)` | Raster sil |
| `api_SetMaxOpenRasterCount(value)` | Maksimum aktif raster sayısı |
| `api_SetRasterONOFF(index, isON)` | Raster aktifliğini değiştir |
| `api_GetRasterONOFF(index)` | Raster aktiflik durumu |
| `api_RasterCount()` | Raster sayısı |
| `api_GetRaster(index)` | Raster al |
| `api_SetRasterOpacity(index, value)` | Raster saydamlığı ayarla |
| `api_GetRasterOpacity(index)` | Raster saydamlığı al |

### Raster Nesnesi Yapısı

```javascript
const rasterObj = {
  url: 'http://tile.openstreetmap.org/{z}/{x}/{y}.png',
  type: GlobeApi.CSRasterTypes.XYZ,
  maxLodLevel: 19,
  minLodLevel: 0,
  opacity: 1.0,
  bbox: [-180, -90, 180, 90]
}

myGlobe.api_AddRaster(rasterObj)
```

---

## Raster Overlay

### Image Overlay

| Metod | Açıklama |
|-------|----------|
| `api_AddImageOverlay(id, url, bbox, color, opacity, rDeg, beforeObject, imgCllbck)` | Resim overlay ekle |
| `api_SetImageOverlayColor(id, color, opacity)` | Renk/saydamlık değiştir |
| `api_ChangeImageOverlayURL(id, imgUrl)` | Resmi değiştir |
| `api_DeleteImageOverlay(id)` | Overlay sil |
| `api_GetImageOverlay(id)` | Overlay al |

### WMS Overlay

| Metod | Açıklama |
|-------|----------|
| `api_AddWMSOverlay(id, WMSUrl, color, opacity, imgSize, beforeObject, WMSCallback)` | WMS overlay ekle |
| `api_SetWMSOverlayColor(id, color, opacity)` | Renk/saydamlık değiştir |
| `api_ChangeWMSOverlayURL(id, WMSUrl)` | WMS URL değiştir |
| `api_DeleteWMSOverlay(id)` | Overlay sil |

---

## Vektör Katmanları

### Metodlar

| Metod | Açıklama |
|-------|----------|
| `api_AddLayer(Layer, beforeObject)` | Katman ekle |
| `api_GetLayer(index or displayName)` | Katman al |
| `api_UpdateLayer(index, layerObj)` | Katman güncelle |
| `api_DeleteLayer(Layer)` | Katman sil |
| `api_DeleteLayers()` | Tüm katmanları sil |
| `api_ReloadLayer(Layer)` | Katmanı yeniden yükle |
| `api_LayerCount()` | Katman sayısı |
| `api_SetLayerOpacity(Layer, value)` | Saydamlık ayarla |
| `api_SetLayerOn(Layer, On)` | Aktifliği değiştir |
| `api_GetLayerOn(Layer)` | Aktiflik durumu |

### Katman Tipleri

| Tip | Açıklama |
|-----|----------|
| `CSLayersTypes.CAS` | CAS formatı |
| `CSLayersTypes.OGC_WFS` | OGC WFS servisi |
| `CSLayersTypes.MVT_XYZ` | MVT XYZ tile servisi |
| `CSLayersTypes.CS_OBJECT_ARRAY` | Object Array formatı |

### Nesne Tipleri

| Tip | Açıklama |
|-----|----------|
| `CSObjectTypes.POINT` | Nokta |
| `CSObjectTypes.LINE` | Çizgi |
| `CSObjectTypes.POLYGON` | Çokgen |
| `CSObjectTypes.SHAPE` | Şekil |
| `CSObjectTypes.ARCAREA` | Arc alan |
| `CSObjectTypes.OBJECT_3D` | 3D nesne |

### Katman Örneği

```javascript
const layer = {
  displayName: 'MyLayer',
  type: GlobeApi.CSLayersTypes.CS_OBJECT_ARRAY,
  objectType: GlobeApi.CSObjectTypes.POINT,
  data: {
    coords: [32, 40, 33, 41],
    attribs: [{name: 'Point1'}, {name: 'Point2'}]
  },
  style: myGlobe.api_GetDefaultLayerStyle()
}

myGlobe.api_AddLayer(layer)
```

---

## Object Array

Küre bazlı çizim desteği sağlayan yüksek performanslı veri yapısı.

### Metodlar

| Metod | Açıklama |
|-------|----------|
| `Add(objectArray, beforeObject)` | Object array ekle |
| `Update(id, objectArray)` | Güncelle |
| `Delete(objectArrayOrId)` | Sil |
| `DeleteAll()` | Tümünü sil |
| `SetData(objectArray, data, canChange)` | Data ayarla |
| `UpdateData(objectArray, type, data, canChange)` | Data güncelle |
| `Get(id)` | Object array al |
| `GetNewId()` | Otomatik id üret |
| `StyleChanged(objectArray)` | Stili güncelle |
| `SetOpacity(objectArray, opacity)` | Saydamlık ayarla |

### Örnek

```javascript
const {ObjectArray} = myGlobe

const objArray = {
  id: ObjectArray.GetNewId(),
  objectType: GlobeApi.CSObjectTypes.POINT,
  data: {
    coords: [32, 40, 33, 41],
    attribs: [{name: 'P1'}, {name: 'P2'}]
  },
  style: ObjectArray.GetDefaultStyle()
}

ObjectArray.Add(objArray)
```

---

## Kullanıcı Nesneleri (User Objects)

### ObjectBuffer Yönetimi

| Metod | Açıklama |
|-------|----------|
| `api_CreateObjectBuffer(bufferName, bufferId)` | ObjectBuffer oluştur |
| `api_AddObjectBuffer(objectBuffer)` | ObjectBuffer ekle |
| `api_ObjectBufferCount()` | ObjectBuffer sayısı |
| `api_GetObjectBuffer(bufferIndex)` | ObjectBuffer al |
| `api_FindObjectBufferById(bufferId)` | ID ile ObjectBuffer bul |
| `api_DeleteObjectBufferById(bufferId)` | ID ile sil |
| `api_DeleteObjectBufferByIndex(bufferIndex)` | Index ile sil |
| `api_DeleteAllObjectBuffers()` | Tümünü sil |
| `api_GetNewObjectBufferId()` | Otomatik id üret |

### Nesne İşlemleri

```javascript
// ObjectBuffer oluştur ve ekle
const myObjectBuffer = myGlobe.api_CreateObjectBuffer(
  "objectBufferName", 
  myGlobe.api_GetNewObjectBufferId()
)
myGlobe.api_AddObjectBuffer(myObjectBuffer)

// Nokta nesnesi ekle
const point = {
  type: GlobeApi.CSObjectTypes.POINT,
  coords: [32, 40],
  style: myGlobe.api_GetDefaultStyle(),
  Fid: 'point1'
}
myObjectBuffer.api_Add(point)
```

### Nesne Tipleri

- **POINT** - Nokta nesnesi
- **LINE** - Çizgi nesnesi
- **POLYGON** - Çokgen nesnesi
- **SHAPE** - Şekil nesnesi (üçgen, dikdörtgen, elips, vb.)
- **ARCAREA** - Arc alan nesnesi
- **MODEL** - 3D model nesnesi

### Şekil Tipleri (CSShapeTypes)

| Tip | Açıklama |
|-----|----------|
| `EQUILATERAL_TRIANGLE` | Eşkenar üçgen |
| `ISOSCELES_TRIANGLE` | İkizkenar üçgen |
| `PERPENDICULAR_TRIANGLE` | Dik üçgen |
| `RECTANGLE` | Dikdörtgen |
| `PENTAGON` | Beşgen |
| `HEXAGON` | Altıgen |
| `OCTAGON` | Sekizgen |
| `ELLIPSE` | Elips |
| `PLUS` | Artı |
| `TRISTAR` | Üç köşeli yıldız |
| `RECSTAR` | Dört köşeli yıldız |
| `PENTASTAR` | Beş köşeli yıldız |
| `SPEECH_BUBBLE` | Konuşma balonu |
| `SHORT_RIGHT_ARROW` | Kısa ok |
| `LONG_RIGHT_ARROW` | Uzun ok |
| `LEFT_RIGHT_ARROW` | Çift yönlü ok |

---

## Track

Büyük nokta setlerini performanslı çizmek için kullanılan yapı.

### Metodlar

| Metod | Açıklama |
|-------|----------|
| `Add(track, beforeObject)` | Track ekle |
| `Delete(track)` | Track sil |
| `UpdateData(track, process, dataOrRule)` | Data güncelle |
| `QueryByScreen(x, y)` | Ekran sorgusu |
| `StyleChanged(track)` | Stili güncelle |
| `Get(id)` | Track al |
| `GetNewId()` | Otomatik id üret |
| `SetOn(track, on)` | Aktifliği değiştir |
| `GetOn(track)` | Aktiflik durumu |
| `GetDefaultStyle()` | Varsayılan stil |

### Örnek

```javascript
const {Track} = myGlobe

Track.Add({
  id: 'track1',
  data: {
    coords: [long1, lat1, long2, lat2],
    coordsZ: [z0, z1],
    attribs: [{a0: 'attribs'}, {a1: 'attribs'}]
  },
  objectType: CSTrackObjectTypes.POINT,
  style: Track.GetDefaultStyle(),
  startLod: 2,
  endLod: 22
})
```

---

## Isı Haritası (Heatmap)

### Rasterize Bazlı

```javascript
myGlobe.api_AddImageOverlayHeatmap({
  id: 'heatmap1',
  data: {
    coords: [long1, lat1, long2, lat2],
    intensity: [0.5, 1.0]
  },
  style: {
    radius: 250, // metre
    weight: 1,
    colorPalette: [
      { color: '#000', density: 0 },
      { color: '#00f', density: 0.33 },
      { color: '#ff0', density: 0.66 },
      { color: '#f00', density: 1 }
    ],
    imageSize: 4096
  }
})
```

### Shader Bazlı

| Metod | Açıklama |
|-------|----------|
| `Heatmap.Add(heatmap)` | Isı haritası ekle |
| `Heatmap.Update(id, heatmap)` | Güncelle |
| `Heatmap.Delete(id)` | Sil |
| `Heatmap.DeleteAll()` | Tümünü sil |
| `Heatmap.Get(id)` | Isı haritası al |
| `Heatmap.StyleChanged(heatmap)` | Stili güncelle |

### Isı Haritası Tipleri

| Tip | Açıklama |
|-----|----------|
| `CSHeatmapTypes.SQUARE_GRID` | Kare grid |
| `CSHeatmapTypes.HEXAGON_GRID` | Altıgen grid |

---

## Plugin Sistemi

Özel WebGL rendering için plugin API.

### Plugin Metodları

| Metod | Açıklama |
|-------|----------|
| `api_RegisterPlugin(plugin)` | Plugin kaydet |
| `api_UnRegisterPlugin(pluginId)` | Plugin kaldır |
| `api_GetPlugin(pluginId)` | Plugin al |
| `api_GetAllPluginsId()` | Tüm plugin ID'leri |

### Plugin Yapısı

```javascript
const myPlugin = {
  id: 'myPlugin',
  
  init: function(gl) {
    // Başlatma kodu
  },
  
  draw3D: function(gl) {
    // 3D çizim kodu
  },
  
  draw2D: function(gl) {
    // 2D çizim kodu
  },
  
  mouseDown: function(e) {
    return false // true: eventi yakala
  },
  
  mouseMove: function(e) {
    return false
  },
  
  mouseUp: function(e) {
    return false
  },
  
  mouseClick: function(e) {
    return false
  },
  
  mouseDblClick: function(e) {
    return false
  },
  
  setGeometry: function(geometryType) {
    // Geometri değişikliği
  },
  
  free: function() {
    // Temizleme kodu
  }
}

myGlobe.api_RegisterPlugin(myPlugin)
```

---

## Çizim Sırası (Draw Order)

### Metodlar

| Metod | Açıklama |
|-------|----------|
| `DrawOrder.GetObj(index)` | Index'teki nesneyi al |
| `DrawOrder.ObjCount()` | Nesne sayısı |
| `DrawOrder.Swap(index1, index2)` | İki nesnenin yerini değiştir |
| `DrawOrder.Move(fromIndex, toIndex)` | Nesneyi taşı |

### Çizim Tipleri

| Tip | Açıklama |
|-----|----------|
| `CSDrawOrderTypes.RASTER` | Raster |
| `CSDrawOrderTypes.RASTER_OVERLAY` | Raster overlay |
| `CSDrawOrderTypes.LAYER` | Vektör katman |
| `CSDrawOrderTypes.PLUGIN` | Plugin |
| `CSDrawOrderTypes.TRACK` | Track |
| `CSDrawOrderTypes.HEATMAP` | Isı haritası |
| `CSDrawOrderTypes.OBJECT_ARRAY` | Object array |

---

## Analiz İşlemleri

### Görünürlük Analizi

```javascript
myGlobe.api_VisibilityAnalysis({
  observerPoint: { x: 32, y: 40 },
  observerHeight: 100,
  targetHeight: 0,
  radius: 5000,
  azimuthStart: 0,
  azimuthEnd: 360,
  callback: function(result) {
    console.log(result)
  }
})

// İptal
myGlobe.api_CancelVisibilityAnalysis()
```

### Yükseklik Profili

```javascript
myGlobe.api_FindProfile({
  coords: [long1, lat1, long2, lat2],
  sampleCount: 100,
  callback: function(result) {
    // result: { points: [...], minZ, maxZ }
  }
})
```

### Görüş Hattı (Line of Sight)

```javascript
myGlobe.api_LineOfSight({
  observerPoint: { x: 32, y: 40, z: 100 },
  targetPoint: { x: 32.1, y: 40.1, z: 50 },
  callback: function(result) {
    // result: { visible, blockPoint }
  }
})
```

### Güneş/Ay Pozisyonu

```javascript
const result = myGlobe.api_CalcSunMoon({
  date: new Date(),
  longitude: 32,
  latitude: 40
})
// result: { sunAzimuth, sunAltitude, moonAzimuth, moonAltitude, ... }
```

---

## Matematik Kütüphanesi

`myGlobe.Math` sınıfı üzerinden erişilir.

### Metodlar

| Metod | Açıklama |
|-------|----------|
| `CalcPolyAreaM2(coords)` | Çokgen alanı hesapla (m²) |
| `GetMidPoint(p1, p2)` | İki nokta arası orta nokta |
| `GetDist2D(p1, p2, mode)` | 2D mesafe hesapla |
| `GetDist3D(p1, p2)` | 3D mesafe hesapla |
| `FindPointByPolar(center, distance, angle)` | Polar koordinatla nokta bul |
| `UpGradeLineDegree(coords, degree)` | Çizgi hassasiyetini artır |
| `UpGradeMultiLineDegree(coords, degree)` | Multi-line hassasiyetini artır |
| `UpGradePolygonDegree(coords, degree)` | Çokgen hassasiyetini artır |
| `PointInPolygon(point, polygon)` | Nokta çokgen içinde mi? |
| `RotatePointAroundCenter(point, center, angle)` | Noktayı döndür |
| `GetAzimuthAngle(p1, p2)` | Azimut açısı hesapla |
| `GetBBOX(coords)` | Sınırlayıcı kutu al |
| `GetPosition(coords)` | Pozisyon al |

### Örnek

```javascript
const area = myGlobe.Math.CalcPolyAreaM2([32, 40, 33, 40, 33, 41, 32, 41, 32, 40])
const dist = myGlobe.Math.GetDist2D({x: 32, y: 40}, {x: 33, y: 41})
const isInside = myGlobe.Math.PointInPolygon({x: 32.5, y: 40.5}, polygonCoords)
```

---

## Koordinat Sistemi

`myGlobe.Coordinates` sınıfı üzerinden erişilir.

### Koordinat Tipleri

| Tip | Açıklama |
|-----|----------|
| `geo` | Ondalık derece |
| `dms` | Derece-Dakika-Saniye |
| `dm` | Derece-Dakika |
| `mgrs` | MGRS |
| `proj` | Projeksiyon |
| `georef` | GeoRef |

### Metodlar

| Metod | Açıklama |
|-------|----------|
| `SetType(type)` | Koordinat tipini ayarla |
| `GetType()` | Koordinat tipini al |
| `SetGeoDigits(digits)` | Ondalık hane sayısı |
| `SetDMSDigits(digits)` | DMS ondalık hane sayısı |
| `GetCoordStr(long, lat)` | Koordinat string'i al |
| `api_GeoToUTM(long, lat)` | Geo -> UTM dönüşümü |
| `api_UTMToGeo(easting, northing, zone)` | UTM -> Geo dönüşümü |
| `api_GetUTMZone(long, lat)` | UTM zone al |
| `api_GeoToGeoRef(long, lat)` | Geo -> GeoRef |
| `api_GeoRefToGeo(georef)` | GeoRef -> Geo |
| `api_GeoToDMS(long, lat)` | Geo -> DMS |
| `api_DMSToGeo(dms)` | DMS -> Geo |

### Örnek

```javascript
myGlobe.Coordinates.SetType('dms')
const coordStr = myGlobe.Coordinates.GetCoordStr(32.5, 40.5)
// "32° 30' 0.00" E, 40° 30' 0.00" N"

const utm = myGlobe.api_GeoToUTM(32.5, 40.5)
// { easting, northing, zone }
```

---

## Birim Sistemi

`myGlobe.Units` sınıfı üzerinden erişilir.

### Metodlar

| Metod | Açıklama |
|-------|----------|
| `SetDistanceUnit(unit)` | Mesafe birimini ayarla |
| `SetAreaUnit(unit)` | Alan birimini ayarla |
| `SetAltitudeUnit(unit)` | Yükseklik birimini ayarla |
| `SetAngleUnit(unit)` | Açı birimini ayarla |
| `SetVolumeUnit(unit)` | Hacim birimini ayarla |
| `DistanceUtoU(value, from, to)` | Mesafe dönüşümü |
| `AreaUtoU(value, from, to)` | Alan dönüşümü |
| `AltitudeUtoU(value, from, to)` | Yükseklik dönüşümü |
| `AngleUtoU(value, from, to)` | Açı dönüşümü |
| `VolumeUtoU(value, from, to)` | Hacim dönüşümü |

### Mesafe Birimleri

| Birim | Açıklama |
|-------|----------|
| `m` | Metre |
| `km` | Kilometre |
| `mi` | Mil |
| `nm` | Deniz mili |
| `ft` | Feet |
| `yd` | Yard |

### Alan Birimleri

| Birim | Açıklama |
|-------|----------|
| `m2` | Metrekare |
| `km2` | Kilometrekare |
| `ha` | Hektar |
| `ac` | Acre |

### Açı Birimleri

| Birim | Açıklama |
|-------|----------|
| `deg` | Derece |
| `rad` | Radyan |
| `mil` | Mil (askeri) |
| `gon` | Gon |

---

## Ekran İşlemleri

### Koordinat İşlemleri

| Metod | Açıklama |
|-------|----------|
| `api_GetCurrentWorldLimit()` | Ekrana düşen harita sınırları |
| `api_GetScreenCenterAsDegree()` | Canvas orta noktası koordinatları |
| `api_GetScreenAsDegree(x, y)` | Piksel -> Derece dönüşümü |
| `api_GetMouseDeg()` | Fare konumu (derece) |
| `api_GetMousePos()` | Fare konumu (piksel) |
| `api_ScrW()` | Canvas genişliği |
| `api_ScrH()` | Canvas yüksekliği |
| `api_GetZClient(long, lat)` | Yükseklik değeri (metre) |
| `api_GetCurrentLOD()` | Mevcut LOD seviyesi |

### Mouse İşlemleri

| Metod | Açıklama |
|-------|----------|
| `api_SetMouseEvents(eventName, callback)` | Mouse eventi ayarla |
| `api_DeleteMouseEvents(eventName)` | Mouse eventi sil |

**Event Tipleri:** `down`, `move`, `up`, `click`, `dblclick`, `zoomstart`, `zoomend`

### Örnek

```javascript
myGlobe.api_SetMouseEvents('click', function(e) {
  const pos = myGlobe.api_GetMouseDeg()
  console.log('Tıklanan koordinat:', pos.lng, pos.lat)
  return false
})
```

### Sorgu İşlemleri

| Metod | Açıklama |
|-------|----------|
| `api_QueryByScreen(x, y)` | Ekran koordinatıyla sorgu |
| `api_QueryByObject_InsideData(dataObj, dataArray)` | Nesne içindeki verileri sorgula |
| `api_QueryByObject_OverlapData(dataObj, dataArray)` | Kesişen verileri sorgula |

---

## Dil Ayarları

```javascript
myGlobe.api_SetLang('tr') // Türkçe
myGlobe.api_SetLang('eng') // İngilizce
```

---

## Renk Formatları

Desteklenen renk formatları:

| Format | Örnek |
|--------|-------|
| RGB | `rgb(255, 255, 255)` |
| RGBA | `rgba(255, 255, 255, 0.5)` |
| HEX (uzun) | `#ffffff` |
| HEX (kısa) | `#FFF` |

> **Not:** Büyük-küçük harf duyarlı değildir.

---

## Versiyon Geçmişi

### v6.0.00
- 2D mod desteği eklendi
- Yeni çizim sırası sistemi
- Heatmap geliştirmeleri

### v5.0.01
- Object array performans iyileştirmeleri
- Yeni analiz parametreleri

### v5.0.00
- Shader bazlı ısı haritası
- 3D nesne desteği genişletildi

### v4.0.00
- Plugin sistemi
- Track yapısı

### v3.0.00
- Küre bazlı object array
- Cluster desteği

---

## Bilinen Sorunlar

- Edge v42.17134.1.0 ve HTML v17.17134 sürümlerinde bazı katmanlar yüklenmiyor
- Yeni font teknikleri için minimum tarayıcı sürüm gereksinimleri mevcut

---

*Bu dokümantasyon WebKüre (CAS Web) API v6.x için hazırlanmıştır.*
