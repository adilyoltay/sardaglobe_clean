# Mouse İşlemleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Metodlar

| Metod | Açıklama |
|-------|----------|
| `api_SetMouseEvents(eventName, callback)` | Mouse event callback'i atar |
| `api_GetMouseEvent(eventName)` | Aktif callback'i döndürür ve listeden çıkarır |
| `api_ClearMouseEvents()` | Tüm mouse event callback'lerini temizler |
| `api_SetMouseWheelMode(zoomToCursor)` | Zoom hedef noktasını belirler |
| `GlobeManager.api_SetMouseWheelDirection(reverse)` | Zoom yönünü tersine çevirir |
| `api_SetMouseCursor(cursor)` | Mouse imleç tipini değiştirir |

## Desteklenen Event Türleri

- `down` - Mouse basıldığında
- `move` - Mouse hareket ettiğinde
- `up` - Mouse bırakıldığında
- `click` - Tıklandığında
- `dblclick` - Çift tıklandığında
- `zoomstart` - Zoom başladığında
- `zoomend` - Zoom bittiğinde

## Örnek: Event Atama

```javascript
// Birden fazla event'e tek callback
myGlobe.api_SetMouseEvents("down move up", function(e) {
  switch(e.type) {
    case "down":
      console.log("Mouse down:", e.x, e.y)
      break
    case "move":
      console.log("Mouse move:", e.x, e.y)
      break
    case "up":
      console.log("Mouse up:", e.x, e.y)
      break
  }
  
  // true dönerse globe davranışı engellenir
  // false dönerse normal devam eder
  return false
})
```

## Callback Event Parametresi

```javascript
{
  type,        // Event tipi
  me,          // MouseEvent nesnesi
  lnglatzValid,// Koordinat geçerli mi
  lng,         // Boylam (derece)
  lat,         // Enlem (derece)
  z,           // Yükseklik (metre)
  x,           // Canvas X (piksel)
  y            // Canvas Y (piksel)
}
```

## Event Silme

```javascript
// Tek event silme
myGlobe.api_SetMouseEvents("down", null)

// Callback'i al ve listeden çıkar
const lastCallback = myGlobe.api_GetMouseEvent("down")

// Tümünü temizle
myGlobe.api_ClearMouseEvents()
```
