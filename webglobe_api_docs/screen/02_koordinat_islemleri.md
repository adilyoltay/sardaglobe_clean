# Koordinat İşlemleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Metodlar

| Metod | Açıklama |
|-------|----------|
| `api_GetCurrentWorldLimit()` | Ekrana düşen harita sınırlarını döndürür |
| `api_GetScreenCenterAsDegree()` | Canvas orta noktasının koordinatlarını verir |
| `api_GetScreenAsDegree(x, y)` | Piksel koordinatından harita koordinatı verir |
| `api_GetMouseDeg()` | Mouse konumunun harita koordinatlarını verir |
| `api_GetMousePos()` | Mouse'un ekran ve canvas koordinatlarını döndürür |
| `api_GetCurrentLookInfo()` | Kamera bakış bilgilerini döndürür |
| `api_ScrW()` | Canvas genişliğini piksel cinsinden verir |
| `api_ScrH()` | Canvas yüksekliğini piksel cinsinden verir |
| `api_GetZClient(long, lat)` | Arazi yüksekliğini metre cinsinden döndürür |
| `api_GetCurrentLOD()` | Mevcut LOD seviyesini tam sayı olarak verir |
| `api_GetCurrentLODWithDecimal()` | Mevcut LOD seviyesini ondalıklı verir |
| `api_SetProjectionLimit(llLong, llLat, urLong, urLat)` | FLAT geometri için projeksiyon limitini ayarlar |

## Dönüş Tipleri

### api_GetCurrentWorldLimit()
```javascript
{
  ur: { x: Number, y: Number },  // Sağ üst (long, lat)
  ll: { x: Number, y: Number }   // Sol alt (long, lat)
}
```

### api_GetScreenCenterAsDegree()
```javascript
{ lng: Number, lat: Number }
// veya geçersizse false
```

### api_GetScreenAsDegree(x, y)
```javascript
{ lng: Number, lat: Number, Z: Number }
// Z: yükseklik (metre)
```

### api_GetMousePos()
```javascript
{
  clientX: Number,   // Pencere X
  clientY: Number,   // Pencere Y
  canvasX: Number,   // Canvas X
  canvasY: Number    // Canvas Y
}
```
