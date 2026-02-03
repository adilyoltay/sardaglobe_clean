# MVT_XYZ Katmanı Composite Layer Ekleme

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Tanım

Bir `MVT_XYZ` katmanının içinde birden çok alt katman bulunabilir. Kullanıcı bunları ekleyebilmek için `style`ı array halinde vermelidir. Ayrıca `objectType`, `MVTXYZName` gibi parametreleri de `style` içinde belirtmelidir. Yine alt katmanların hangi LOD(Level of Detail) aralığında çizileceğini belirleyebilir, bunun için `startlod` ve `endLod` parametrelerini de `style` içinde verebilir. Kullanıcı `api_GetDefaultCompositeLayerStyle()` metodunu kullanarak varsayılan composite layer stilini alabilir.

Verilen style sıralaması dikkate alınarak composite layer'lar üst üste eklenir. Örneğin 2 elemanlı bir style array'i verilmişse `style[0]`da tanımlanan katman alta `style[1]` de tanımlanan katman üste eklenir.

## Örnek Kullanım 1

```javascript
 const style = []

 style.push(myGlobe.api_GetDefaultCompositeLayerStyle())
 style.push(myGlobe.api_GetDefaultCompositeLayerStyle())

 style[0].objectType = CSObjectTypes.POLYGON
 style[0].MVTXYZName = 'landuse_overlay'
 style[0].startLod = 5
 style[0].endLod = 12
 style[0].fillColor = '#ffff00'
 style[0].border = false

 style[1].objectType = CSObjectTypes.LINE
 style[1].MVTXYZName = 'road'
 style[1].startLod = 2
 style[1].endLod = 19
 style[1].borderColor = '#000'
 style[1].lineType.width = 2

const MVTCompositeLayer={
  layerType  :myGlobe.api_LAYER_TYPE_MVT_XYZ(),
  url        : "https://b.tiles.mapbox.com/v4/...",
  style,
  bbox       : null,
  startLod   : 2,
  endLod     : 22,
  continuousLOD:true,
  rasterize:true,
  reportObj: function(params, event) {
    console.log(params, event)
  },
}
myGlobe.api_AddLayer(MVTCompositeLayer)

```

>[!SCODE|label:MVT Composite Layer Resim|]


<img height="400" width="600" src="assets/img/MVTComposite.jpg" data-origin="MVTComposite.jpg" alt="MVT Composite Layer">

## Örnek Kullanım 2 (Composite Filter)

```javascript
 const style = []

 style.push(myGlobe.api_GetDefaultCompositeLayerStyle())
 style.push(myGlobe.api_GetDefaultCompositeLayerStyle())

 style[0].objectType = CSObjectTypes.POLYGON
 style[0].MVTXYZName = 'landuse_overlay'
 style[0].startLod = 5
 style[0].endLod = 12
 style[0].fillColor = '#ffff00'
 style[0].border = false

 style[1].objectType = CSObjectTypes.LINE
 style[1].MVTXYZName = 'road'
 style[1].startLod = 2
 style[1].endLod = 19
 style[1].borderColor = '#000'
 style[1].lineType.width = 2

const MVTCompositeLayer={
  id : myGlobe.api_GetNewLayerId(),
  layerType  :myGlobe.api_LAYER_TYPE_MVT_XYZ(),
  url        : "https://b.tiles.mapbox.com/v4/...",
  style,
  bbox       : null,
  startLod   : 2,
  endLod     : 22,
  continuousLOD:true,
  rasterize:true,
  filter : [ ['all', ['!=', 'type', 'trunk']], ['all', ['==', 'type', 'motorway']] ]
  reportObj: function(params, event) {
    console.log(params, event)
  },
}
myGlobe.api_AddLayer(MVTCompositeLayer)

```

>[!SCODE|label:MVT Composite Layer Filter Resim|]


<img height="400" width="600" src="assets/img/compositeFilter.jpg" data-origin="compositeFilter.jpg" alt="MVT Composite Filter">

## MVT_XYZ Katmanı Composite Layer'ların Görünürlüğünü Açıp Kapatma

`api_SetCompositeLayerVisibility(Layer, MVTXYZName, visibility)` metodu kullanılarak verilen `MVTXYZName`e uyan alt katmanın görünürlüğü açılıp kapatılabilir. `visibility` parametresi `true` ise verilen `MVTXYZName`e uyan alt katman çizilir, `false` ise çizilmez.

```javascript
myGlobe.api_SetCompositeLayerVisibility(MVTCompositeLayer, 'landuse_overlay', false)

```
>[!SCODE|label: Alt Katman Görünürlüğü Açma Kapatma Resim|]

<div style="display:flex; justify-content: center; align-items:center;">

<table style="display:inline">
<caption align="bottom">landuse_overlay: Aktif</caption>
<tr><td><img width=480 height=300 src="assets/img/compositeActive.jpg" alt="compositeActive.jpg"/></td></tr>
</table>


<table style="display:inline; ">
<caption align="bottom">landuse_overlay: Pasif</caption>
<tr><td><img width=480 height=300  src="assets/img/compositePassive.jpg" alt="compositePassive.jpg"/></td></tr>
</table>

</div>
