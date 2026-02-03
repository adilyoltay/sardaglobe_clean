# WMS Overlay

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Tanım

Dışarıdan verilen bir WMS adresinin raster haline getirilerek kürenin kaplanmasıdır. Kullanıcının dünyaya baktığı alan değiştikçe otomatik olarak indirilerek kaplanır.

## Metodlar

| Metod | Açıklama |
|-------|----------|
| `api_AddWMSOverlay(id, WMSUrl, color, opacity, imgSize, beforeObject, WMSCallback)` | WMS overlay ekler |
| `api_SetWMSOverlayColor(id, color, opacity)` | Renk ve saydamlık değiştirir |
| `api_ChangeWMSOverlayURL(id, WMSUrl)` | WMS URL'sini değiştirir |
| `api_DeleteWMSOverlay(id)` | WMS overlay'ı siler |

## Parametreler

| Parametre | Açıklama |
|-----------|----------|
| `id` | WMS Overlay raster'ın ID değeri |
| `WMSUrl` | WMS adresi. URL'ye sadece `&LAYERS=value` bilgisi eklenmesi yeterlidir |
| `opacity` | Saydamlık değeri. Varsayılan: 1 |
| `imgSize` | Resim boyutu. Maksimum: 2048 |
| `color` | Renk. `null` ise orijinal renkler kullanılır |
| `beforeObject` | Çizim sırası için referans nesne |
| `WMSCallback` | Yükleme callback fonksiyonu |

## Örnekler

### WMS Overlay Ekleme

```javascript
const id = 0
const WMSUrl = "http://sampledomain/csglobe/csogc.dll/wms?&LAYERS=bluemarble"
const opacity = 1
const color = null
const imageSize = 512
const beforeObject = null

myGlobe.api_AddWMSOverlay(id, WMSUrl, color, opacity, imageSize, beforeObject, function(imgOk) {
  console.log("WMS loaded:", imgOk)
})
```

### Renk ve Saydamlık Güncelleme

```javascript
const id = 0
const color = '#17DF93'
const opacity = 0.7
myGlobe.api_SetWMSOverlayColor(id, color, opacity)
```

### URL Güncelleme

```javascript
const id = 0
const WMSUrl = "http://sampledomain/csglobe/csogc.dll/wms?&LAYERS=Raster"
myGlobe.api_ChangeWMSOverlayURL(id, WMSUrl)
```

### Silme

```javascript
myGlobe.api_DeleteWMSOverlay(0)
```

## Renk Tipleri

Desteklenen formatlar:
- **RGB:** `rgb(255,255,255)`
- **HEX (uzun):** `#ffffff`
- **HEX (kısa):** `#FFF`
