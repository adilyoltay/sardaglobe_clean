# Tanım

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

`DrawOrder`; raster, raster overlay, vektör katmanları ve plugin'lerin çizim sıralarını değiştirebilmek, çizim sırasındaki nesneleri alabilmek için tasarlanmış kütüphanedir.

DrawOrder metodları `myGlobe.DrawOrder` sınıfından çağırılarak kullanılır.
