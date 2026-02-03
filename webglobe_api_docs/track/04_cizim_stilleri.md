# Track Çizim Stilleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Style Yapısı

```javascript
{
  type: CSTrackPointTypes.SQUARE,
  size: 6,           // piksel
  ratio: 2,          // RECTANGLE için
  color: '#fff',
  opacity: 1,
  filter: [],
  fidKey: ''
}
```

## Style Parametreleri

| Parametre | Açıklama |
|-----------|----------|
| `type` | Nokta tipi: `SQUARE`, `CIRCLE`, `RECTANGLE` |
| `size` | Çizim boyutu (piksel). Varsayılan: 6 |
| `ratio` | RECTANGLE için en/boy oranı. Varsayılan: 2 |
| `color` | Renk. Varsayılan: beyaz |
| `opacity` | Saydamlık (0-1). Varsayılan: 1 |
| `filter` | Filtreleme kuralları |
| `fidKey` | Seçim için öznitelik anahtarı |

## Point Tipleri

```javascript
const CSTrackPointTypes = {
  SQUARE,     // Kare (varsayılan)
  CIRCLE,     // Daire
  RECTANGLE   // Dikdörtgen
}
```

## Style Filter Örneği

```javascript
style.filter = [
  {
    rule: ['all', ['LESS', 'fid', 1260]],
    name: 'sample_filter',
    style: {
      color: '#f00',
      opacity: 0.5
    }
  }
]
```

## Selected Style Yapısı

```javascript
{
  labels: [{
    text: '',
    textCallback: null,
    offset: { x: 16, y: -16 },
    vAlignment: CSVerticalAlignment.TOP,
    hAlignment: CSHorizontalAlignment.LEFT,
    size: 20,
    hollowColor: '#000',
    textColor: '#FFF',
    textOpacity: 1,
    fontFamily: {
      name: 'arial',
      bold: true,
      italic: false,
      hollow: true,
      hollowWidth: 2
    },
    canMove: false
  }],
  size: 6,
  color: '#f00',
  opacity: 0.75,
  shape: null,
  filter: []
}
```

## Shape Parametreleri (selectedStyle)

| Parametre | Açıklama |
|-----------|----------|
| `color` | Şekil rengi |
| `opacity` | Şekil saydamlığı |
| `scale` | Ölçek katsayısı |
| `borderWidth` | Çizgi kalınlığı (piksel). Varsayılan: 3 |
