# LOD Display ve Support URL

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## lodDisplay Yapısı

LOD aralıklarına göre farklı servislerden görüntü göstermek için kullanılır.

```javascript
rasterObj.lodDisplay = [
  {
    startLod: 0,
    endLod: 5,
    url: ['http://sampledomain/rasterTile/raster/ecw/{z}/{x}/{y}.png']
  },
  {
    startLod: 6,
    endLod: 10,
    url: [
      'http://a.tile.openstreetmap.org/{z}/{x}/{y}.png',
      'http://b.tile.openstreetmap.org/{z}/{x}/{y}.png'
    ]
  }
]
```

### lodDisplay Parametreleri

| Parametre | Açıklama |
|-----------|----------|
| `startLod` | Başlangıç LOD seviyesi |
| `endLod` | Bitiş LOD seviyesi |
| `url` | Bu LOD aralığında kullanılacak URL(ler) |

> **Uyarı:** Kameranın LOD değeri `lodDisplay`de verilen aralıkta mevcut değilse, herhangi bir raster katmanı gösterilmez.

## supportURL Yapısı

Boş veya hatalı tile'ları yedek URL ile desteklemek için kullanılır.

```javascript
rasterObj.supportURL = {
  url: [
    'http://a.tile.openstreetmap.org/{z}/{x}/{y}.png',
    'http://b.tile.openstreetmap.org/{z}/{x}/{y}.png'
  ],
  emptyContentSupport: true,
  notFoundLodRangeSupport: true,
  transparentPixelSupport: true,
  outOfBBOXSupport: true
}
```

### supportURL Parametreleri

| Parametre | Açıklama |
|-----------|----------|
| `url` | Destek URL(leri) |
| `emptyContentSupport` | Boş/hatalı tile'ları destekle |
| `notFoundLodRangeSupport` | Verilmeyen LOD aralıklarını destekle |
| `transparentPixelSupport` | Saydam pikselleri destekle |
| `outOfBBOXSupport` | BBOX dışını destekle |

## Renk Tipleri

Desteklenen renk formatları:
- **RGB:** `rgb(255,255,255)`
- **HEX (uzun):** `#ffffff`
- **HEX (kısa):** `#FFF`

> Büyük küçük harf duyarlı değildir. Geçersiz renk girildiğinde varsayılan beyaz renk uygulanır.
