# Örnekler

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## 2 Nesnenin Çizim Sırasını Değiştirme Örneği

```javascript
const layer = myGlobe.api_GetLayerById('layerId')  
const raster = myGlobe.api_GetRasterById('rasterId')
// layer ile raster'ın çizim sıralarını değiştirir
myGlobe.DrawOrder.Swap(layer, raster)
```

## Nesnenin Çizim Sırasını Taşıma Örneği

```javascript
const layer = myGlobe.api_GetLayerById('layerId')  
const raster = myGlobe.api_GetRasterById('rasterId')
// layer'ı raster'ın altına alır
myGlobe.DrawOrder.Move(layer, raster)
```

>[!WARNING]
>2d çizimler(icon ve yazılar) listedeki çizim sırası farketmeksizin 3d çizimler bittikten sonra çizilir. 3d çizimler kendi aralarında, 2d çizimler ise kendi aralarında sıralanarak çizilir.
