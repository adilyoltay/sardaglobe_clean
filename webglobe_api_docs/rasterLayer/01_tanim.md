# Raster Katmanları

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Tanım

Raster, ayrıntıların düzenli aralıklı hücreler biçiminde oluşturulduğu, çizgi haritaların taraması ile elde edilen sayısal harita türüdür.

Raster katmanları iki ana tipte desteklenir:
- **XYZ_MERCATOR**: Web Mercator Tile raster tipi
- **WMS**: Web Map Service raster tipi

## Raster Tipleri

| Parametre | Açıklama |
|-----------|----------|
| `WMS` | WMS raster tipi için kullanılan değer |
| `XYZ_MERCATOR` | Web Mercator Tile raster tipi için kullanılan değer. Çoklu adres verilebilir |

> **Not:** WMS rasterlar için bulut gibi yarı saydam raster'ları göstermek istediğinizde url'ye `&FORMAT=image/png&TRANSPARENT=true` ekleyebilirsiniz.
