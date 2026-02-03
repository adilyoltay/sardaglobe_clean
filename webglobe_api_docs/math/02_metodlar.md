# Metodlar

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

| Metod                                 | Açıklama                                                                                                  |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| `CalcPolyAreaM2(coords, is3D, stepLimit)`        | Polygon'un alanını döndürür. |
| `GetMidPoint(long1, lat1, long2, lat2)`        | 2 noktanın küresel orta noktasını bulur. `{long, lat}` döndürür.|
| `GetDist2D(long1, lat1, long2, lat2, mode)`        | 2 noktanın 2d mesafesini metre cinsinden döndürür.|
| `GetDist3D(long1, lat1, long2, lat2)`        | 2 noktanın küresel 3d mesafesini metre cinsinden döndürür.|
| `FindPointByPolar(long, lat, dist, azimuthAngle)`        | Başlangıç noktasından mesafe ve açı kadar gidilen noktayı verir. `{long, lat}` döndürür.|
| `UpGradeLineDegree(long1, lat1, long2, lat2, numLOD)`        | 2 nokta arasında küresel noktalar bulur.|
| `UpGradeMultiLineDegree(coords, numLOD, isClosed)`        | Koordinat dizisi arasında küresel noktalar bulur.|
| `UpGradePolygonDegree(coords, gfCoords, stepLimit)`| Koordinat dizisi için iç noktalar üretir.|
| `PointInPolygon(long, lat, coords)`        | Noktanın polygon içinde olup olmadığını kontrol eder.|
| `RotatePointAroundCenter(long, lat, cnLong, cnLat, azimuthAngle)`        | Noktayı merkez etrafında döndürür. `{long, lat}` döndürür.|
| `GetAzimuthAngle(long1, lat1, long2, lat2)`        | 2 nokta arasındaki kuzey açısını derece cinsinden döndürür.|
| `GetBBOX(coords)`| Koordinat dizisinin sınırlarını döndürür. `{ur:{ x, y}, ll:{ x, y}}`|
| `GetPosition(coords, type)`| Koordinat dizisinin pozisyonunu verilen tipe göre döndürür. `{long, lat}`|

## 2d Mesafe Ölçme Modları

| Type| Açıklama|
|-------------------|------------|
|  GREAT_CIRCLE     | Küresel Great Circle algoritmasına göre mesafe bulur. Varsayılan değerdir. |
|  GEODESIC         | Jeodezik mesafe hesaplar. |

```javascript
const CSDistance2DMode = {
  GREAT_CIRCLE: 0,
  GEODESIC: 1
}
```

## Pozisyon Tipleri

| Type| Açıklama|
|-------------------|------------|
|  VERTEX_CENTER        | Ağırlık merkezi |
|  LIMIT_CENTER         | Limitlerin orta noktası |
|  LIMIT_LEFT_TOP       | Sol üst nokta |
|  LIMIT_LEFT_CENTER    | Sol orta nokta |
|  LIMIT_LEFT_BOTTOM    | Sol alt nokta |
|  LIMIT_RIGHT_TOP      | Sağ üst nokta |
|  LIMIT_RIGHT_CENTER   | Sağ orta nokta |
|  LIMIT_RIGHT_BOTTOM   | Sağ alt nokta |
|  LIMIT_TOP_CENTER     | Üst orta nokta |
|  LIMIT_BOTTOM_CENTER  | Alt orta nokta |
|  LEFT_VERTEX          | En soldaki nokta |
|  RIGHT_VERTEX         | En sağdaki nokta |
|  TOP_VERTEX           | En üstteki nokta |
|  BOTTOM_VERTEX        | En alttaki nokta |
|  GEOM_FIRST           | İlk nokta |
|  GEOM_CENTER          | Orta nokta |
|  GEOM_LAST            | Son nokta |

```javascript
const CSTextPositionTypes = {
  VERTEX_CENTER: 0,
  LIMIT_CENTER: 1,
  LIMIT_LEFT_TOP: 2,
  LIMIT_LEFT_CENTER: 3,
  LIMIT_LEFT_BOTTOM: 4,
  LIMIT_RIGHT_TOP: 5,
  LIMIT_RIGHT_CENTER: 6,
  LIMIT_RIGHT_BOTTOM: 7,
  LIMIT_TOP_CENTER: 8,
  LIMIT_BOTTOM_CENTER: 9,
  LEFT_VERTEX: 10,
  RIGHT_VERTEX: 11,
  TOP_VERTEX: 12,
  BOTTOM_VERTEX: 13,
  GEOM_FIRST: 14,
  GEOM_CENTER: 15,
  GEOM_LAST: 16
}
```
