# Track Metodları

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Temel Metodlar

| Metod | Açıklama |
|-------|----------|
| `Add(track, beforeObject)` | Track nesnesini ekler |
| `Delete(track)` | Track nesnesini siler |
| `UpdateData(track, process, dataOrRule)` | Track datasını günceller |
| `QueryByScreen(x, y)` | Ekran koordinatına göre sorgu atar |
| `StyleChanged(track)` | Stili günceller |
| `Get(id)` | ID'ye göre track nesnesini döndürür |
| `GetNewId()` | Otomatik ID üretir |
| `SetOn(track, on)` | Aktifliği değiştirir |
| `GetOn(track)` | Aktiflik durumunu döndürür |
| `GetDefaultStyle()` | Varsayılan stili döndürür |

## Seçim Metodları

| Metod | Açıklama |
|-------|----------|
| `SetDrawMode(track, drawMode, fidList)` | Çizim modunu değiştirir |
| `GetDrawMode(track, fidList)` | Çizim modunu döndürür |
| `GetDefaultSelectedStyle()` | Varsayılan seçim stilini döndürür |

## Label Metodları

| Metod | Açıklama |
|-------|----------|
| `api_CanMoveLabelsByMouse(canMove)` | Yazıların mouse ile taşınmasını kontrol eder |
| `api_CanResetLabelsByMouse(canReset)` | Yazıların sağ tıkla sıfırlanmasını kontrol eder |
| `ResetAllLabels()` | Tüm yazı pozisyonlarını sıfırlar |

## UpdateData Process Tipleri

| Tip | Açıklama |
|-----|----------|
| `CSTrackUpdateProcessTypes.ADD` | Data eklenir |
| `CSTrackUpdateProcessTypes.DELETE` | Data silinir |

```javascript
const CSTrackUpdateProcessTypes = {
  ADD,
  DELETE
}
```
