# Raster Overlay

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Tanım

Raster overlay dışarıdan verilen bir resmin ya da WMS adresinin raster haline getirilerek kürenin kaplanmasıdır. Resimler tek seferde indirilip kaplanır, bu yüzden resim boyutları WebGL'in izin verdiği boyutlardan büyük olmamalıdır.

**Tavsiye edilen en büyük raster kenar uzunluğu:** 2048 piksel

Raster overlay iki tipe ayrılır:
- **Image Overlay:** Dışarıdan verilen bir resmin raster haline getirilmesi
- **WMS Overlay:** WMS adresinin raster haline getirilmesi
