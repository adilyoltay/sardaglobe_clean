# Metodlar

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

| Metod                                 | Açıklama                                                                                                  |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| `GetObj(drawIndex)`                   | Çizim sırası verilen nesneyi döndürür.|
| `ObjCount()`                          | Çizim listesindeki eleman sayısını verir.|
| `Swap(firstObject, secondObject)`     | İki nesnenin çizim sırasını değiştirir. |
| `Move(object, beforeObject)`          | Nesneyi belirtilen nesnenin altında çizer. |

## Çizim Tipleri

| `type` Type| Açıklama|
| ---------- | ------------- |
| `RASTER` |  Nesnenin tipi raster'dır.  |
| `RASTER_OVERLAY` | Nesnenin tipi raster overlay'dir. |
| `LAYER` | Nesnenin tipi layer'dır. |
| `PLUGIN` |  Nesnenin tipi plugin'dir. |
| `TRACK` |  Nesnenin tipi track'tır. |
| `HEATMAP` |  Shader bazlı ısı haritası. |
| `OBJECT_ARRAY` |  CS_OBJECT_ARRAY katman tipinin küre bazlı versiyonu. |

```javascript
const CSDrawTypes = {
  RASTER: 0,
  RASTER_OVERLAY: 1,
  LAYER: 2,
  PLUGIN: 3,
  TRACK: 4,
  HEATMAP: 5,
  OBJECT_ARRAY: 6
}
```
