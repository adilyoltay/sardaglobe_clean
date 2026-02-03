# Vektör Katman Ekleme

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

```javascript

const style = myGlobe.api_GetDefaultLayerStyle()

const PointLayer={
  id : myGlobe.api_GetNewLayerId(),
  displayName: null,
  layerType: myGlobe.api_LAYER_TYPE_OGC_WFS(),
  objectParams: null,
  wkbGeom: "GEOM",,
  wfsLayerName: "sampleName",
  url: "http://sampledomain/wfs?SERVICE=WFS&VERSION=1.0.0&REQUEST=GetFeature&outputFormat=GML2", // &TYPENAME=nokta",
  style: style,
  objectType: CSObjectTypes.POINT, // yada CSObjectTypes.LINE yada CSObjectTypes.POLYGON yada "model"
  bbox: [26, 36, 45, 42],
  filter: null,
  startLod: 12,
  endLod: 17,
  reportObj: function(values, mouseEvent){
    console.log(values.obj.Fid)
    console.log(values.layer)
    console.log(values.obj)
    console.log(values.screenX)
    console.log(values.screenY)
    console.log(values.lng)
    console.log(values.lat)
  }
}

myGlobe.api_AddLayer(PointLayer)

```


## Katman Tipleri İçin Ortak Varsayılan Özellikler:

| Özellik        | Açıklama        |
|---------------------------|---------------------------|
| `layerType`      | [Katman tipleri](/vectorLayer/?id=katman-tiplerine-göre-Özellikler)   |
| `objectType`     | Katmanda gösterilecek olan nesne tipidir. Layer objectType'lar için [bakınız](/vectorLayer/?id=katmanda-desteklenen-nesne-tipleri)      |
|`displayName`     | Katman ismi|
| `objectParams`   | Vektör katmanında geometri tanımı belirsiz nesneleri tekrar oluşturmak için kullanılan yöntemdir. |
| `filter`| Belirlenen filtre kuralına göre gelen nesnelerden hangilerinin bu katmana ekleneceğini belirler. |
| `style`          | Layer'ın gösterim özellikleridir. [Layer Stilleri](/vectorLayer/?id=vektör-Çizim-stilleri)   |
| `bbox`           | Verilen bbox sınırlarına düşen nesneleri harita üzerinde gösterir. |
| `startLod`       | Katmanın görünürlüğünün başlangıç `LOD`(Level Of Detail) seviyesi. Varsayılan değer 2'dir. |
| `endLod`         | Katmanın görünürlüğünün bitiş `LOD` seviyesi. Varsayılan değer 25'tir. |
| `query`         | `boolean` değer alır, `query` değeri `true` verildiğinde vektör katmanlarındaki tüm nesneler için sorgu atılır. Varsayılan değeri `true`dur. |
| `reportObj(function)` | reportObj özelliği layer nesnelerinin iconlarının üzerine, kenarlarına ya da içi dolu nesnelerin içine tıklandığında çağrılan fonksiyondur. |
|`id`     | Katmanın `id`sidir. |


## Katman Tiplerine Göre Özellikler:

| LayerType                      |            Katman tipine göre eklenecek diğer parametreler: |
|--------------------------------|--------------------------------|
|  `api_LAYER_TYPE_OGC_WFS`   |  `wkbGeom`, `wfsLayerName`, `url` |
|  `api_LAYER_TYPE_CAS_LAYER`  |   `CASTable`, `url` |
|  `api_LAYER_TYPE_MVT_XYZ`  |   `MVTXYZName`, `continuousLOD`, `rasterize`, `showParentData`, `url` |
| `api_LAYER_TYPE_CS_OBJECT_ARRAY`|  `clusterStyle`, `data` |

## Katmanda Desteklenen Nesne Tipleri

Katmanda desteklenen nesne tipleri nokta, çizgi ve alan'dır. Katmanın `objectType` parametresine değeri `CSObjectTypes` nesnesi kullanılarak nokta, çizgi ya da alan çizilebilir. Ayrıca `CS_OBJECT_ARRAY` katman tipi shape, arcArea ve OBJECT_3D nesne tiplerini de destekler.

>[!SCODE|label:Katman Ekleme Örneği|]


```javascript

const style = myGlobe.api_GetDefaultLayerStyle()
style.labels[0].text="'${fid}'"

const PointLayer={
  id : myGlobe.api_GetNewLayerId(),
  layerType     : myGlobe.api_LAYER_TYPE_OGC_WFS(),
  objectType    : CSObjectTypes.POINT,
  wkbGeom       : "GEOM",
  wfsLayerName  : "koyler",
  objectParams  : {}
  url           : "http://sampledomain/wfs?SERVICE=WFS&VERSION=1.0.0&REQUEST=GetFeature&outputFormat=GML2",
  style         : style,
  bbox          : [26,36,45,42], // Türkiye sınırları
  filter        : null,
  startLod      : 12,
  endLod        : 17,
  reportObj     : function(values, mouseEvent){
    console.log(values.obj.Fid)
    console.log(values.layer)
    console.log(values.obj)
    console.log(values.screenX)
    console.log(values.screenY)
    console.log(values.lng)
    console.log(values.lat)
  }
}

myGlobe.api_AddLayer(PointLayer)

```

>[!SCODE|label:WFS Katmanı Resim|]


<img height="400" width="600" src="assets/img/layerOrnek.jpg" data-origin="layerOrnek.jpg" alt="Shape Ekleme">
