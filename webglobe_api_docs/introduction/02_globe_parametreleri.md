# Globe Parametreleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Globe parametreleri, Globe'un gerçeklenmesi için gerekli olan parametrelerdir.

```javascript

const globeParameters = {
  canvas: document.getElementById("globe"),
  geometry: GlobeApi.CSGeometryTypes.SPHERE, // ya da GlobeApi.CSGeometryTypes.FLAT
  globeMaxLodLevel: 19,
  raster: {
            url: "http://sampledomain/csglobe/csogc.dll/wms?&SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&SRS=EPSG:3857&LAYERS=Raster&STYLES=",
            type: GlobeApi.CSRasterTypes.WMS,
            maxLodLevel: 18,
            opacity: 1.0,
            bbox: [-180, -90, 180, 90],
          },
  startEmptyRaster: true,
  emptyRasterColor:'#000',
  milIconTexturizeType: GlobeApi.CSMilIconTexturizeTypes.LINEAR,
  rasterizeLayerQuality: GlobeApi.CSRasterizeQuality.LOW,
  mesh: {
             url: "http://sampledomain/csglobeMesh",
             type: GlobeApi.CSMeshTypes.WGS84
  },
  skybox:{
             url:'http://sampledomain/skybox/',
             imageType:GlobeApi.CSSkyBoxImageTypes.JPG
  },
  options: {
             showStatusBar: true,
             showCompass: true,
             showOverview: true,
             showScaleBar: true,
             showDebug: true
  },
  analysisURL: {
             los: 'https://sampledomain/mesh_service/los',
             visibility: 'https://sampledomain/mesh_service/viewshed',
             profile: 'https://sampledomain/mesh_service/profile'
  },
  globedll: "http://sampledomain/anotherdirectory/webglobeserver.dll/",
}

```

>[!TIP]
>globeParameters'te verilen `raster`, Raster listesinde 0. index'te tutulmaktadır.

## Parametre Açıklamaları

| Parametre | Açıklama                                            |
| --------- | --------------------------------------------------- |
| `canvas`    | Html sayfasında oluşturulan canvas elementi.        |
| `geometry`    | Globe'un hangi geometri tipinde gösterileceğini belirtir.     |
| `globeMaxLodLevel`    | Globe'un kaçıncı LOD seviyesine kadar parçalanacağını belirtir.   |
|`raster`     | Raster yapısı |
| `startEmptyRaster`    | Globe'un oluşturulma anında raster verileri gelene kadar kürenin hangi şekilde gösterileceğini belirler. |
| `emptyRasterColor`    | Raster verileri gelene kadar kürenin gösterileceği renk. |
| `milIconTexturizeType`    | Askeri semboller için dokulaştırma tipi. |
| `rasterizeLayerQuality`    | Rasterize vektör katmanlarının çizim kalitesi. |
| `mesh` | Yükseklik verileri için URL ve tip. |
| `skybox` | Arka plan için skybox ayarları. |
| `options` | Durum çubuğu, pusula, önizleme paneli gibi araçların gösterim ayarları. |
| `analysisURL` | Görüş analizi, profil alma gibi analizler için URL'ler. |
|`globedll`| Kimliklendirme için gerekli dll dosya URL'i |

## Javascript Gerçekleştirimi

```javascript
import { GlobeApi } from '@pirireis/webglobe'

const globeParameters = {
  canvas: document.getElementById("globe"),
  geometry: GlobeApi.CSGeometryTypes.SPHERE,
  globeMaxLodLevel:17,
  raster: {
            url: "http://sampledomain/csglobe/csogc.dll/wms?&SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&SRS=EPSG:3857&LAYERS=Raster&STYLES=",
            type: GlobeApi.CSRasterTypes.WMS,
            maxLodLevel: 17,
            opacity: 1.0,
            bbox: [-180, -90, 180, 90],
          },
  startEmptyRaster: false,
  rasterizeLayerQuality: GlobeApi.CSRasterizeQuality.LOW,
  milIconTexturizeType: GlobeApi.CSMilIconTexturizeTypes.LINEAR,
  mesh: {
             url: "http://sampledomain/csglobeMesh",
             type: GlobeApi.CSMeshTypes.WGS84
  },
  skybox:{
             url:'http://sampledomain/skybox/',
             imageType:GlobeApi.CSSkyBoxImageTypes.JPG
  },
  analysisURL: {
             los: 'https://sampledomain/mesh_service/los',
             visibility: 'https://sampledomain/mesh_service/viewshed',
             profile: 'https://sampledomain/mesh_service/profile'
  },
  globedll: "http://sampledomain/anotherdirectory/webglobeserver.dll/",
}
const myGlobe = new GlobeApi.CSGlobe(globeParameters)
```

>[!WARNING]
>Eğer bir lisans problemi olursa Globe ekranda titreyerek çizilecektir.
