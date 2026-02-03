# Object Array Çizim Stilleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Stil Yapısı

```javascript
{
  labels: [
           {
              startLod: 2,
              endLod: 25,
              position: CSTextPositionTypes.VERTEX_CENTER,
              text: '',
              textCallback : null,
              autoOffset : false,
              offset: { x: 16, y: -16 },
              vAlignment: CSVerticalAlignment.TOP,
              hAlignment: CSHorizontalAlignment.LEFT,
              size: 20,
              hollowColor: '#000',
              textColor: '#FFF',
              textOpacity: 1,
              transform: CSTextTransformMode.NONE,
              fontFamily: {
                  customName:null,
                  name: 'arial',
                  bold: true,
                  italic: false,
                  hollow: true,
                  hollowWidth: 2,
                  hollowOpacity : 0.75,
                  hollowBlur : 1
              },
              canMove : false,
              ignoreEmptyStrings: false,
              textStyle: null
           }
          ],
  heading : null,
  iconType: CSIconTypes.NOICON,
  milIcon: [],
  milIconParams: null,
  active: true,
  icon: {
    name: '',
    mapName: '',
    sizeX: 32,
    sizeY: 32,
    rotDeg: 0,
    integerPosition : false,
    rotateByGlobe : false
  },
  filter: [],
  lodDisplay : [],
  opacity: 1,
  border: true,
  borderColor: '#f4d6f6',
  filled: false,
  fillColor: '#ffff00',
  bottomFilled: false,
  bottomFillColor: '#ffff00',
  sideFilled: true,
  sideColor: '#ff00ff',
  cullFace: true,
  depthTest: false,
  zMode: CSZMode.Z_GROUND_PERVERTEX,
  shape: [],
  fidKey:'',
  flashIcon:false,
  flashLabels : false,
  flashGeo :false,
  startLod: 2,
  endLod: 25
}
```

## Stil Parametreleri

| Parametre | Açıklama |
| --------- | -------- |
| `labels` | Yazı ayarları dizisi |
| `heading` | Hız vektörü ayarları |
| `iconType` | Icon tipi (NOICON, MAP) |
| `milIcon` | Askeri sembol kodu |
| `milIconParams` | Askeri sembol parametreleri |
| `active` | Filtre kuralına uymayan nesnelerin çizilip çizilmeyeceği |
| `icon` | Icon ayarları |
| `filter` | Filtre kuralları |
| `lodDisplay` | LOD bazlı görünüm ayarları |
| `opacity` | Saydamlık değeri |
| `border` | Kenar çizgisi |
| `borderColor` | Kenar rengi |
| `filled` | İç dolgu |
| `fillColor` | Dolgu rengi |
| `bottomFilled` | Alt yüzey dolgusu (3D) |
| `bottomFillColor` | Alt yüzey rengi (3D) |
| `sideFilled` | Yan yüzey dolgusu (3D) |
| `sideColor` | Yan yüzey rengi (3D) |
| `cullFace` | Arka yüzey görünürlüğü |
| `depthTest` | Derinlik testi |
| `zMode` | Z modu (Z_MSL, Z_GROUND_PERVERTEX) |
| `shape` | Şekil nesnesi ayarları |
| `fidKey` | Seçim için benzersiz anahtar |
| `flashIcon` | Icon yanıp sönme |
| `flashLabels` | Yazı yanıp sönme |
| `flashGeo` | Geometri yanıp sönme |
| `startLod` | Başlangıç LOD seviyesi |
| `endLod` | Bitiş LOD seviyesi |

## Icon Tipleri

| Icon Type | Açıklama |
|-----------|----------|
| `NOICON` | Icon değerleri kullanılmaz |
| `MAP` | IconMap'ten icon kullanılır |

```javascript
const CSIconTypes = {
  NOICON,
  MAP
}
```

## Z Modu

| Z Modu | Açıklama |
|--------|----------|
| `Z_MSL` | Deniz seviyesine göre çizilir |
| `Z_GROUND_PERVERTEX` | Vertex'lerden araziye uyumlu |

## Yazı Dikey Konum Tipleri

| vAlignment | Açıklama |
|------------|----------|
| `TOP` | Offset değerlerinin yukarısında |
| `BOTTOM` | Offset değerlerinin aşağısında |
| `CENTER` | Dikeyde ortalanmış |

```javascript
const CSVerticalAlignment = {
  TOP,
  BOTTOM,
  CENTER
}
```

## Yazı Yatay Konum Tipleri

| hAlignment | Açıklama |
|------------|----------|
| `LEFT` | Offset değerlerinin sağında |
| `RIGHT` | Offset değerlerinin solunda |
| `CENTER` | Yatayda ortalanmış |

```javascript
const CSHorizontalAlignment = {
  LEFT,
  RIGHT,
  CENTER
}
```

## Font Transform Tipleri

| Transform | Açıklama |
|-----------|----------|
| `NONE` | Dönüşüm yok |
| `UPPERCASE` | Büyük harfe dönüştür |
| `LOWERCASE` | Küçük harfe dönüştür |

```javascript
const CSTextTransformMode = {
  NONE,
  UPPERCASE,
  LOWERCASE
}
```

## Renk Tipleri

- **RGB**: `rgb(255,255,255)`
- **HEX long**: `#ffffff`
- **HEX short**: `#FFF`

## Distance Tipleri

| Distance Type | Kısaltma |
|---------------|----------|
| centimeters | 'cm' |
| meter | 'm' |
| kilometer | 'km' |
| groundmile | 'mile' |
| seamile | 'nm' |
| inch | 'in' |
| feet | 'ft' |
| yard | 'yd' |

## Açı Tipleri

| Angle Type | Kısaltma |
|------------|----------|
| degree | '°' |
| radyan | 'rad' |
| grad | 'g' |
| angularmil | 'mil' |
| minuteofarc | '\'' |

## Filtre Örneği

```javascript
objectArrayStyle.filter = [
  {
    rule: [ 'all', ['LESS', 'fid', 1260] ],
    name: 'sample_filter',
    style: {
      icon: {
        mapName: 'iconMap_name',
        name: 'iconmap_iconName',
        sizeX: 32,
        sizeY: 32
      },
      startLod : 6,
      endLod : 12
    },
  }
]
```

## lodDisplay Örneği

```javascript
objectArrayStyle.lodDisplay = [
  {
    LOD: 3,
    text:{
      scale: 0.5,
      color: '#ca00fd',
      hollowColor: '#00ded0'
    },
    icon:{
      scale: 0.5
    },
    headingScale: 1,
    opacity : 0.5,
    fillColor : '#0000ff',
    borderColor: '#00ffff'
  },
  {
    LOD: 10,
    text:{
      scale: 1,
      color: '#dd00fd',
      hollowColor: '#0000f0'
    },
    icon:{
      scale:1
    },
    headingScale: 2,
    opacity : 1,
    fillColor : '#000000',
    borderColor: '#ffffff'
  }
]
```
