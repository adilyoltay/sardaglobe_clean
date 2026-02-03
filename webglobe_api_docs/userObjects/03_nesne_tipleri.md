# Nesne Tipleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Nokta Nesnesi (POINT)

```javascript
{
  type: CSObjectTypes.POINT,
  Fid: "sampleId",
  attribs: { "key1": "value1" },
  coords: [32.0, 40.0],        // [long, lat]
  coordsZ: [0],                // Yükseklik (metre)
  startLod: 0,
  endLod: 19,
  style: myGlobe.api_GetDefaultStyle(),
  reportObj: function(params, event) { }
}
```

## Çizgi Nesnesi (LINE)

```javascript
{
  type: CSObjectTypes.LINE,
  Fid: "sampleId",
  attribs: { "key1": "value1" },
  coords: [30.0, 36.0, 31.0, 37.0, 32.0, 36.5],  // [long1, lat1, long2, lat2, ...]
  coordsZ: [0, 0, 0],
  startLod: 0,
  endLod: 19,
  style: myGlobe.api_GetDefaultStyle(),
  reportObj: function(params, event) { }
}
```

## Çokgen Nesnesi (POLYGON)

```javascript
{
  type: CSObjectTypes.POLYGON,
  Fid: "sampleId",
  attribs: { "key1": "value1" },
  coords: [30.0, 36.0, 31.0, 36.0, 31.0, 37.0, 30.0, 37.0],
  coordsZ: [0, 0, 0, 0],
  startLod: 0,
  endLod: 19,
  solid3D: false,
  heights: null,
  fixedHeights: false,
  fixedTop: false,
  style: myGlobe.api_GetDefaultStyle(),
  reportObj: function(params, event) { }
}
```

## Şekil Nesnesi (SHAPE)

```javascript
{
  type: CSObjectTypes.SHAPE,
  shapeType: CSShapeTypes.ELLIPSE,
  Fid: "sampleId",
  coords: [32.0, 40.0],        // Merkez noktası
  coordsZ: [0],
  proportional: false,
  solid3D: false,
  fixedTop: false,
  heights: null,
  startLod: 0,
  endLod: 19,
  stepAng: 10,
  radius1: 1000,               // X ekseni yarıçapı (metre)
  radius2: 1000,               // Y ekseni yarıçapı (metre)
  rotDeg: 0,                   // Dönme derecesi
  style: myGlobe.api_GetDefaultStyle(),
  reportObj: function(params, event) { }
}
```

## Şekil Türleri (CSShapeTypes)

| Tip | Açıklama |
|-----|----------|
| `EQUILATERAL_TRIANGLE` | Eşkenar üçgen |
| `ISOSCELES_TRIANGLE` | İkizkenar üçgen |
| `PERPENDICULAR_TRIANGLE` | Dik üçgen |
| `RECTANGLE` | Dikdörtgen |
| `PENTAGON` | Beşgen |
| `HEXAGON` | Altıgen |
| `OCTAGON` | Sekizgen |
| `ELLIPSE` | Elips |
| `PLUS` | Artı işareti |
| `TRISTAR` | Üç köşeli yıldız |
| `RECSTAR` | Dört köşeli yıldız |
| `PENTASTAR` | Beş köşeli yıldız |
| `SPEECH_BUBBLE` | Konuşma balonu |
| `SHORT_RIGHT_ARROW` | Kısa sağ ok |
| `LONG_RIGHT_ARROW` | Uzun sağ ok |
| `LEFT_RIGHT_ARROW` | Çift yönlü ok |

## Ortak Parametreler

| Parametre | Açıklama |
|-----------|----------|
| `Fid` | Nesne ID'si |
| `attribs` | Öznitelikler (macro için kullanılabilir) |
| `coords` | Koordinatlar (derece) |
| `coordsZ` | Yükseklik değerleri (metre) |
| `startLod` | Başlangıç LOD. Varsayılan: 0 |
| `endLod` | Bitiş LOD. Varsayılan: 25 |
| `style` | Gösterim özellikleri |
| `reportObj` | Tıklama callback fonksiyonu |
