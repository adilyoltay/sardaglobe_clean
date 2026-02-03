# Raster Yapısı

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Temel Raster Nesnesi

```javascript
{
  id: myGlobe.api_GetNewRasterId(),
  url: "http://example.com/tiles/{z}/{x}/{y}.png",
  lodDisplay: null,
  supportURL: null,
  type: GlobeApi.CSRasterTypes.XYZ_MERCATOR,
  maxLodLevel: 18,
  opacity: 1.0,
  noDatatoEmptyImage: false,
  bbox: [-180, -90, 180, 90]
}
```

## Parametreler

| Parametre | Açıklama |
|-----------|----------|
| `id` | Raster'ın ID değeri. `api_GetNewRasterId()` ile otomatik üretilebilir |
| `url` | Raster için gerekli adres. WMS veya XYZ_MERCATOR tipinde olmalı |
| `lodDisplay` | LOD aralıklarına göre farklı servislerden görüntü gösterir |
| `supportURL` | Boş tile'ları desteklemek için yedek URL yapısı |
| `type` | Raster tipi: `WMS` veya `XYZ_MERCATOR` |
| `maxLodLevel` | Maksimum LOD seviyesi |
| `opacity` | Saydamlık değeri (0-1). Varsayılan: 1 |
| `noDatatoEmptyImage` | Veri olmayan LOD'larda hata oluşmasını engeller |
| `bbox` | Sınır kutusu: `[LL.x, LL.y, UR.x, UR.y]` |
| `postRequest` | XYZ_MERCATOR için POST istek parametreleri |

## XYZ_MERCATOR Örneği

```javascript
{
  id: myGlobe.api_GetNewRasterId(),
  url: "http://a.tile.openstreetmap.org/{z}/{x}/{y}.png",
  type: GlobeApi.CSRasterTypes.XYZ_MERCATOR,
  maxLodLevel: 18,
  opacity: 1.0,
  bbox: [-180, -90, 180, 90]
}
```

## WMS Örneği

```javascript
{
  id: myGlobe.api_GetNewRasterId(),
  url: "http://sampledomain/csglobe/csogc.dll/wms?&SERVICE=WMS&VERSION=1.1.1&REQUEST=GetMap&SRS=EPSG:3857&LAYERS=Raster&STYLES=",
  type: GlobeApi.CSRasterTypes.WMS,
  maxLodLevel: 18,
  opacity: 1.0,
  bbox: [-180, -90, 180, 90]
}
```

## Multi URL Örneği

```javascript
{
  id: myGlobe.api_GetNewRasterId(),
  url: [
    'http://a.tile.openstreetmap.org/{z}/{x}/{y}.png',
    'http://b.tile.openstreetmap.org/{z}/{x}/{y}.png',
    'http://c.tile.openstreetmap.org/{z}/{x}/{y}.png'
  ],
  type: GlobeApi.CSRasterTypes.XYZ_MERCATOR,
  maxLodLevel: 18,
  opacity: 1.0
}
```
