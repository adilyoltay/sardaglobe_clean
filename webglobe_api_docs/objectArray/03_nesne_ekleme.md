# Object Array Nesnesi Ekleme

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

```javascript
const { ObjectArray } = myGlobe

const objArray={
  id : ObjectArray.GetNewId(),
  displayName: 'Point Object Array',
  style : ObjectArray.GetDefaultStyle(),
  selectedStyle : ObjectArray.GetDefaultSelectedStyle(),
  data: [
    {
      coords: [32, 40, 34, 42, 35, 41],
      coordsZ: [0, 0, 0],
      attribs: [{}, {}, {}]
    }
  ],
  objectType : CSObjectTypes.POINT,
  filter: null,
  startLod   : 2,
  endLod     : 19,
  query:true,
  reportObj: function(values, mouseEvent){}
}
ObjectArray.Add(objArray)
```

## Parametreler

| Parametre        | Açıklama        |
|---------------------------|---------------------------|
| `objectType`     | Nesne tipi (şu an sadece `CSObjectTypes.POINT` desteklenir) |
| `data`     | Nesne verileri |
|`displayName`     | Nesne ismi |
| `filter`| Filtre kuralları |
| `style`          | Gösterim özellikleri |
| `selectedStyle`  | Seçim modundaki gösterim özellikleri |
| `startLod`       | Başlangıç LOD seviyesi (varsayılan 2) |
| `endLod`         | Bitiş LOD seviyesi (varsayılan 25) |
| `query`         | Sorgu atılıp atılmayacağı (varsayılan true) |
| `reportObj(function)` | Tıklama callback fonksiyonu |
|`id`     | Nesne ID'si |

## Data Parametreleri

|Parametre| Açıklama|
|-------------|-------------|
|`coords`       | Boylam ve enlem değerleri (derece) |
|`coordsZ`      | Yükseklik değerleri (metre) |
|`attribs`      | Öznitelikler (macro için kullanılabilir) |

## Point Nesnesinin Veri Yapısı

```javascript
const dataStructure =[{
    coords:[x0,y0,x1,y1,x2,y2],
    coordsZ:[z0,z1,z2],
    attribs:[{a0:"attribs"}, {a1:"attribs"}, {a2:"attribs"}],
  }]
```
