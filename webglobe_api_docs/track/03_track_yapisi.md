# Track Yapısı

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Temel Track Nesnesi

```javascript
{
  id: 'track-id',
  data: {
    coords: [long1, lat1, long2, lat2, ..., longN, latN],
    coordsZ: [z0, z1, ..., zN],
    attribs: [{a0: "attribs"}, {a1: "attribs"}, ..., {aN: "attribs"}]
  },
  objectType: CSTrackObjectTypes.POINT,
  style: myGlobe.Track.GetDefaultStyle(),
  selectedStyle: myGlobe.Track.GetDefaultSelectedStyle(),
  startLod: 2,
  endLod: 25
}
```

## Parametreler

| Parametre | Açıklama |
|-----------|----------|
| `id` | Track nesnesinin ID değeri |
| `data.coords` | Koordinatlar: `[long1, lat1, long2, lat2, ...]` |
| `data.coordsZ` | Yükseklik değerleri (metre) |
| `data.attribs` | Öznitelik dizisi |
| `objectType` | Nesne tipi. Şu an sadece `POINT` desteklenir |
| `style` | Gösterim özellikleri |
| `selectedStyle` | Seçim modundaki gösterim özellikleri |
| `startLod` | Başlangıç LOD seviyesi. Varsayılan: 2 |
| `endLod` | Bitiş LOD seviyesi. Varsayılan: 25 |

## Örnek: Track Ekleme

```javascript
const { Track } = myGlobe

const selectedStyle = Track.GetDefaultSelectedStyle()
selectedStyle.size = 20
selectedStyle.color = '#00f'
selectedStyle.labels[0].text = 'Selected Text'

Track.Add({
  id: 'my-track',
  data: {
    coords: [32.0, 40.0, 33.0, 41.0, 34.0, 40.5],
    coordsZ: [0, 100, 200],
    attribs: [{name: "Point 1"}, {name: "Point 2"}, {name: "Point 3"}]
  },
  objectType: CSTrackObjectTypes.POINT,
  style: Track.GetDefaultStyle(),
  selectedStyle,
  startLod: 2,
  endLod: 22
})
```
