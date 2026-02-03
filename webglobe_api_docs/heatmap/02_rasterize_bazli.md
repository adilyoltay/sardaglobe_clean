# Rasterize Bazlı Isı Haritası

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Kullanıcının verdiği değerlere göre bir ısı haritası resmi oluşturulur ve ısı haritaları, arazi yüzeyine kaplanarak çizilir.

## Metod

| Metod                        | Açıklama     |
| -----------------------------| ----------------|
| `api_AddImageOverlayHeatmap(heatmap, beforeObject)`| Isı haritasını ekler. Silmek için `api_DeleteImageOverlay(id)` kullanılır. |

## Heatmap Yapısı

```javascript
{
  id,
  data : {
    coords: [long1, lat1, long2, lat2],
    intensity: [intensity1, intensity2], // opsiyonel
  }
  style: {
    radius: 250, // metre cinsinden
    weight: 1,
    colorPalette: [
            { color: '#000', density: 0 },
            { color: '#00f', density: 0.33 },
            { color: '#ff0', density: 0.66 },
            { color: '#f00', density: 1 }
              ],
    imageSize: 4096,
    backgroundOpacity: 0,
    backgroundColor: '#fff'
  }
}
```

## Parametreler

|Parametre           |Açıklama            |
|--------------------|--------------------|
|`id`               |  Isı haritasının id değeri. |
|`coords`| Pozisyonlar [long1, lat1, long2, lat2] |
|`intensity`| Her nokta için hassaslık değeri (0-1 arası) |
|`radius`| Noktaların yarıçapı (metre) |
|`weight`| Global ağırlık değeri |
|`colorPalette`| Renk paleti |
|`imageSize`| Resim boyutu (varsayılan 4096, max 8192) |

## Örnek

```javascript
myGlobe.api_AddImageOverlayHeatmap({
  id: 'overlayHeatmap',
  data: {
    coords : [long1, lat1, long2, lat2, ....., longN, latN],
  },
  style: {
    radius: 2000,
    weight: 1,
    colorPalette: [
      { color: 'rgb(0, 0, 0)', density: 0 },
      { color: 'rgb(0, 0, 255)', density: 0.33 },
      { color: 'rgb(255, 255, 0)', density: 0.66 },
      { color: 'rgb(255, 0, 0)', density: 1 }
    ],
    imageSize: 4096
 }
})
```
