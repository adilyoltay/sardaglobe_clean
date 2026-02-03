# Vektör Katman Nesne Seçimlerinde Çizim Stilleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Tanım

Vektör katmanları için nesne seçimlerinde uygulanacak çizim stili yapısı `api_GetDefaultLayerSelectedStyle()` metodu ile alınabilir.

## Seçim Stili Parametreleri

| Parametre | Açıklama                                            |
| --------- | --------------------------------------------------- |
| `labels` | Seçili nesnelerin yazı ayarları |
| `icon` | Seçili nesnelerin icon ayarları |
| `mouseOverSymbolScale` | Mouse üzerindeyken ölçeklendirme |
| `filter` | Seçim filtre kuralları |
| `opacity` | Seçili nesnelerin saydamlık değeri (varsayılan 0.75) |
| `borderColor` | Seçili nesnelerin kenar rengi |
| `fillColor` | Seçili nesnelerin dolgu rengi |
| `bottomFillColor` | 3D nesneler için alt yüzey rengi |
| `sideColor` | 3D nesneler için yan yüzey rengi |
| `shape` | Seçili nesnelerin etrafına çizilecek şekil |
| `flashIcon` | Seçili icon yanıp sönme |
| `flashLabels` | Seçili yazı yanıp sönme |
| `flashGeo` | Seçili geometri yanıp sönme |
| `flashShape` | Seçili şekil yanıp sönme |

## SelectedStyle Yapısı İçin Filter Bölümü

`selectedStyle` içindeki özelliklerin istenilen değerleri `filter.style` içerisinde değiştirilebilir.

```javascript
selectedStyle.filter = [
  {
    name: 'sample_filter',
    style: {
      icon: {
        mapName: 'iconMap_name'
        name: 'iconmap_iconName',
        sizeX: 32,
        sizeY: 32
      },
    },
  }
]
```

>[!WARNING]
>Katmanların `selectedStyle` bölümünde olmayıp `style` bölümünde olan parametreler, çizim stillerini `reduction` parametreleri dışında nesnenin style bölümünden alırlar.
