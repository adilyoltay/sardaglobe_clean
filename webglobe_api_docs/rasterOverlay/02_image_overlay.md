# Image Overlay

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Metodlar

| Metod | Açıklama |
|-------|----------|
| `api_AddImageOverlay(id, url, bbox, color, opacity, rDeg, beforeObject, imgCllbck)` | Resim rasterlaştırılarak eklenir |
| `api_SetImageOverlayColor(id, color, opacity)` | Renk ve saydamlık değiştirir |
| `api_ChangeImageOverlayURL(id, imgUrl)` | Resmi değiştirir |
| `api_DeleteImageOverlay(id)` | Raster'ı siler. `null` verilirse hepsini siler |
| `api_GetImageOverlay(id)` | ID'ye göre raster'ı döndürür |

## Parametreler

| Parametre | Açıklama |
|-----------|----------|
| `id` | Raster'ın ID değeri |
| `url` | Resmin URL'si (png, jpg, svg, base64 desteklenir) |
| `bbox` | Sınır kutusu: `{ ll: { x, y }, ur: { x, y } }` |
| `opacity` | Saydamlık değeri. Varsayılan: 1 |
| `rDeg` | Dönme derecesi. Varsayılan: 0 |
| `color` | Resim rengi. `null` ise orijinal renkler kullanılır |
| `beforeObject` | Çizim sırası için referans nesne |
| `imgCllbck` | Yükleme callback fonksiyonu |

## Örnekler

### Image Overlay Ekleme

```javascript
const id = 0
const imgUrl = "http://sampledomain.com/icon.png"
const bbox = { ll: { x: 26, y: 36 }, ur: { x: 45, y: 42 } }
const opacity = 1
const rotDeg = 0
const color = null
const beforeObject = null

myGlobe.api_AddImageOverlay(id, imgUrl, bbox, color, opacity, rotDeg, beforeObject, function(imgOk) {
  console.log("Image loaded:", imgOk)
})
```

### Renk ve Saydamlık Güncelleme

```javascript
const id = 0
const color = '#D71616'
const opacity = 0.6
myGlobe.api_SetImageOverlayColor(id, color, opacity)
```

### Silme

```javascript
myGlobe.api_DeleteImageOverlay(0)      // Tek bir overlay sil
myGlobe.api_DeleteImageOverlay(null)   // Tümünü sil
```
