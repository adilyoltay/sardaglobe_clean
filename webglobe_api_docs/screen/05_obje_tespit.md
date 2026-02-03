# Obje Tespit İşlemleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Metodlar

| Metod | Açıklama |
|-------|----------|
| `api_QueryByScreen(x, y)` | Piksel koordinatına düşen nesneleri döndürür |
| `api_SetQueryBoxSize(value)` | Yakalama kutusu boyutunu ayarlar. Varsayılan: 8 |
| `api_GetQueryBoxSize()` | Yakalama kutusu boyutunu döndürür |
| `api_QueryByBBox(userBbox, queryType, isCover)` | BBOX içindeki nesneleri döndürür |
| `api_QueryByGeometry(geometryCoords, queryType, isCover)` | Geometri içindeki nesneleri döndürür |
| `api_SetObjectHint(showFunc, cancelFunc)` | Mouse ile nesne üzerine gelindiğinde hint gösterir |
| `api_GetObjectLimit(object)` | Nesnenin limit değerlerini döndürür |
| `api_CanPickPoint(screenX, screenY, long, lat, Z, isMSL)` | Nesnenin yakalanıp yakalanamayacağını kontrol eder |
| `api_ObjectToPolygon(objectDefinition)` | Nesnenin gerçek çizim koordinatlarını hesaplar |

## Inside/Overlap Sorguları

| Metod | Açıklama |
|-------|----------|
| `api_QueryByObject_InsideGlobe(objectDefinition, useZ)` | Nesne içinde kalan tüm nesneleri döndürür |
| `api_QueryByObject_InsideSelectionSet(objectDefinition, useZ, selectionObjArray)` | Seçim setinden nesne içinde kalanları döndürür |
| `api_QueryByObject_InsideData(objectDefinition, useZ, dataArray)` | Data array'den nesne içinde kalanları döndürür |
| `api_QueryByObject_OverlapGlobe(objectDefinition, useZ)` | Nesneyle çakışan tüm nesneleri döndürür |
| `api_QueryByObject_OverlapSelectionSet(objectDefinition, useZ, selectionObjArray)` | Seçim setinden çakışanları döndürür |
| `api_QueryByObject_OverlapData(objectDefinition, useZ, dataArray)` | Data array'den çakışanları döndürür |

## Sorgu Dönüş Yapısı

```javascript
[
  {
    obj,       // Nesne
    owner,     // ObjectBuffer, layer veya object array
    ownerType  // Owner tipi (CSOwnerTypes)
  }
]
```

## Owner Tipleri (CSOwnerTypes)

```javascript
const CSOwnerTypes = {
  OBJECT_BUFFER: 0,
  VECTOR_LAYER: 1,
  OBJECT_ARRAY: 2
}
```

## Query Type Parametresi

- `inside`: Alanın içine düşen nesneler
- `overlap`: Alanla kesişen nesneler

## isCover Parametresi (overlap modunda)

- `true`: İçindeki + kesişen + kapsayan nesneler
- `false`: Sadece içindeki + kesişen nesneler (kapsayan hariç)
