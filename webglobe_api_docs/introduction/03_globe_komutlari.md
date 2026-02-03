# Globe Komutları ve Tipler

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Globe için Kullanılan Komutlar

|Komut            |Açıklama            |
|-----------------|-----------------|
|`api_SetGeometry(geometryType)`| Haritanın geometri tipini değiştirir. Küre ve düzlem olmak üzere 2 geometri tipi vardır.|
|`api_GlobeVersion()`| Globe versiyon numarasını verir. |
|`api_GlobeFree()`  | Globe nesnesi yok edilmeden önce kullanılan bufferların silinmesi için gerekli olan api komutu.|

## Geometri Tipleri

| Geom Type| Açıklama|
|-------------------|------------|
|  SPHERE           | Harita küre geometri tipinde gösterilir. Varsayılan değeri `SPHERE`dır. |
|  FLAT             | Harita düzlem geometri tipinde gösterilir. Çizimler WEB Mercator projeksiyonuna göre yapılır. |

```javascript
const CSGeometryTypes = {
  SPHERE,
  FLAT
}
```

## Mesh Tipleri

| Icon Type| Açıklama|
|-------------------|------------|
|  WGS84           | Dünya Jeodezik Sistemi-1984 (World Geodetic System-1984)|
|  XYZ_MERCATOR    | Web Mercator Tile mesh tipi için kullanılan değer|

```javascript
const CSMeshTypes = {
  WGS84,
  XYZ_MERCATOR
}
```

### Mesh İçin Kullanılan Komutlar

|Komut            |Açıklama            |
|-----------------|-----------------|
|`api_SetMeshCacheSize(value)`| cache'te tutulacak eleman sayısını değiştirir. Varsayılan değeri 1000'dir. |
|`api_ReTryAtMeshTimeout(reTry, continueMeshDiv, callback)`| Timeout'a ya da hataya düşen mesh'ler için yeniden indirme isteği yapılıp yapılmayacağı durumunu değiştirir. |

## SkyBox imageType Tipleri

| imageType| Açıklama|
|-------------------|------------|
|  JPG           | Resimler jpg formatındadır. |
|  PNG           | Resimler png formatındadır.|

```javascript
const CSSkyBoxImageTypes = {
  JPG,
  PNG
}
```

## Askeri Sembol Dokulaştırma Tipleri

| Type| Açıklama|
|-------------------|------------|
|  LINEAR              | Pürüzsüz ve yumuşak görüntü. Varsayılan değerdir.|
|  NEAREST           | Keskin ve bloklu görüntü. |

```javascript
const CSMilIconTexturizeTypes = {
  LINEAR: 0,
  NEAREST: 1
}
```

## Rasterize Çizim Kalite Modları

| Icon Type| Açıklama|
|-------------------|------------|
|  LOW              | Düşük çözünürlük modu. Varsayılan değerdir.|
|  MEDIUM           | Orta çözünürlük modu. LOW'a göre 4 kat fazla bellek kullanımı.|
|  HIGH             | Yüksek çözünürlük modu. LOW'a göre 16 kat fazla bellek kullanımı.|

```javascript
const CSRasterizeQuality = {
  LOW,
  MEDIUM,
  HIGH
}
```

## emptyRasterColor Renk Tipleri

- **RGB**: `rgba(255, 255, 255, 0.5)`
- **HEX long**: `#ffffff`
- **HEX short**: `#FFF`

## SkyBox Örneği

```javascript
const globeParameters = {
  canvas: document.getElementById("globe"),
  globeMaxLodLevel:19,
  raster: {
            url: "http://sampledomain/csglobe/csogc.dll/wms?...",
            type: GlobeApi.CSRasterTypes.WMS,
            maxLodLevel: 19,
            opacity: 1.0,
            bbox: [-180, -90, 180, 90],
          },
  mesh: {
    url: "http://sampledomain/csglobeMesh",
    type: GlobeApi.CSMeshTypes.WGS84
  },
  skybox:{
    url:'http://sampledomain/skybox/',
    imageType:GlobeApi.CSSkyBoxImageTypes.JPG
  },
  globedll: "http://sampledomain/anotherdirectory/webglobeserver.dll/",
}
const myGlobe = new GlobeApi.CSGlobe(globeParameters)
```
