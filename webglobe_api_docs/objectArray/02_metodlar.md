# Metodlar

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

| Metod                        | Açıklama     |
| -----------------------------| ----------------|
| `Add(objectArray, beforeObject)`        |   Nesneyi ekler |
| `Update(id, objectArray)`     |   Nesneyi günceller |
| `Delete(objectArrayOrId)`     |   Nesneyi siler |
| `DeleteAll()`         |   Tüm nesneleri siler |
| `SetData(objectArray, data, canChange)` | Data'yı güncelleyerek uygular |
| `UpdateData(objectArray, type, data, canChange)` | Data'yı günceller |
| `Get(id)`              |   ID'ye göre nesne döndürür |
| `GetNewId()`              |   Otomatik ID üretir |
| `StyleChanged(objectArray)`       |   Stili günceller |
| `SetOpacity(objectArray, opacity)`       |   Saydamlığı değiştirir |
| `SetFlash(objectArray, flashIcon, flashLabels, flashGeo, flashShape)`| Yanıp sönme değerlerini değiştirir |
| `ResetAllLabels()`| Yazı pozisyonlarını resetler |
| `SetOn(objectArray, on)`       |   Aktifliği değiştirir |
| `GetOn(objectArray)`       |   Aktiflik bilgisini verir |
| `GetLimitsOfLoadedObjects(objectArray)`       | Yüklü nesnelerin limitlerini verir |
| `ObjCount()`             |  Nesne sayısını verir |
| `GetDefaultStyle()`| Varsayılan stili verir |
| `GetStyle(objectArray)`       | Stili JSON olarak döndürür |

## Seçim Metodları

| Metod                        | Açıklama     |
| -----------------------------| ----------------|
| `CheckDrawModeChanges(objectArray)`| Çizim modu değişikliklerini uygular |
| `SetSelectedList(objectArray, fidList)`| Seçim listesini ayarlar |
| `GetSelectedList(objectArray)`     | Seçim listesini döndürür |
| `SetSelectionFlash(objectArray, flashIcon, flashLabels, flashGeo, flashShape)`| Seçim yanıp sönme değerlerini ayarlar |
| `SetSelectionOpacity(objectArray, opacity)`       | Seçim saydamlığını değiştirir |
| `GetDefaultSelectedStyle()`             | Varsayılan seçim stilini verir |

## Nesne Metodları

| Metod                        | Açıklama     |
| -----------------------------| ----------------|
| `setDrawMode(drawMode, immediateUpdate)`|  Çizim modunu değiştirir |
| `getDrawMode()`| Çizim modunu döndürür |
