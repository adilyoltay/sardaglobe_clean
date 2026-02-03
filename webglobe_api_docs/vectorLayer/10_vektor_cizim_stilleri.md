# Vektör Çizim Stilleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Tanım

Vektör çizim stili yapısı `api_GetDefaultLayerStyle()` metodu ile alınabilir.

## Stil Parametreleri

| Parametre | Açıklama                                            |
| --------- | --------------------------------------------------- |
| `labels` | Yazı ayarları (text, offset, size, color, fontFamily vb.) |
| `heading` | Hız vektörü çizimi için kullanılır |
| `iconType` | Icon tipi (NOICON, MAP, CIRCLE) |
| `milIcon` | Askeri sembol kodu |
| `milIconParams` | Askeri sembol parametreleri |
| `active` | Nesnelerin çizilip çizilmeyeceği |
| `icon` | Icon ayarları (mapName, name, sizeX, sizeY, radius vb.) |
| `iconReduction` | Icon seyreltme ayarları |
| `mouseOverSymbolScale` | Mouse üzerindeyken ölçeklendirme |
| `filter` | Filtre kuralları |
| `lodDisplay` | LOD bazlı görünüm ayarları |
| `opacity` | Saydamlık değeri |
| `border` | Kenar çizgisi |
| `borderColor` | Kenar rengi |
| `filled` | İç dolgu |
| `fillColor` | Dolgu rengi |
| `bottomFilled` | 3D nesneler için alt yüzey |
| `bottomFillColor` | Alt yüzey rengi |
| `sideFilled` | 3D nesneler için yan yüzey |
| `sideColor` | Yan yüzey rengi |
| `cullFace` | Arka yüzey görünürlüğü |
| `depthTest` | Derinlik testi |
| `zMode` | Z modu (Z_MSL, Z_GROUND_PERVERTEX) |
| `lineType` | Çizgi özellikleri (cap, join, dash, width) |
| `shape` | Nesne etrafına çizilecek şekil |
| `fidKey` | Seçim için benzersiz anahtar |
| `flashIcon` | Icon yanıp sönme |
| `flashLabels` | Yazı yanıp sönme |
| `flashGeo` | Geometri yanıp sönme |
| `flashShape` | Şekil yanıp sönme |
| `startLod` | Başlangıç LOD seviyesi |
| `endLod` | Bitiş LOD seviyesi |

## Vektör Layer Stil İcon Tipleri

| Icon Type| Açıklama|
|-------------------|------------|
|  NOICON           | Icon değerleri kullanılmaz. |
|  MAP              | Eklenen iconMap'ten icon kullanılmak istendiğinde kullanılır.|
|  CIRCLE           | Stil değerleriyle birlikte dinamik olarak circle çizimi için kullanılır. |

## Vektör Layer Stil Z Modu

| Z modu  | Açıklama      |
| ---------- | ------------- |
| `Z_MSL` |  Nesne deniz seviyesine göre çizilir. |
| `Z_GROUND_PERVERTEX` |  Nesne sadece eklenen vertexlerinden araziye uyumlu hale gelir. |

## Vektör Layer Stil Renk Tipleri

- **RGB**: `rgb(255,255,255)`
- **HEX long**: `#ffffff`
- **HEX short**: `#FFF`

## Farklı Font Alfabesi Ekleme

Latin alfabesinden farklı alfabeleri kullanmak için custom fontlar eklenmelidir.
`api_AddCustomFont(customName, customFontParams)` metodu kullanılır.

```javascript
const customName = "kiril"
const customFontParams = {
    name: 'arial',
    bold: true,
    italic: false,
    chars: 'абдЖчдефгğхıижклмноёпрсштуювйз'
    undefinedChar:'?'
}

myGlobe.api_AddCustomFont(customName, customFontParams)

const layerStyle = myGlobe.api_GetDefaultLayerStyle()
layerStyle.labels[0].fontFamily.customName = 'kiril'
```

## Çoklu Satır Desteği

Layer `style` nesnesinde yazıları `\n` ile ayırarak vermek yeterlidir.

```javascript
const layerStyle = myGlobe.api_GetDefaultLayerStyle()
layerStyle.iconType = CSIconTypes.MAP
layerStyle.icon.mapName = 'light'
layerStyle.icon.name = 'lightning25'
layerStyle.labels[0].text = 'Sağ Üst\nÇoklu Satır'
```

## IconMap Ekleme

IconMap birden fazla iconun bulunduğu tek resim dosyasıdır.

```javascript
api_AddIconMap('IconMapName', ICON_MAP_URL, ICON_MAP_JSON_URL)
```

## Circle Icon Tipini Ekleme

```javascript
const style = myGlobe.api_GetDefaultLayerStyle()
style.iconType = CSIconTypes.CIRCLE
style.icon.borderColor= '#FFF'
style.icon.fillColor= '#F00'
style.icon.borderWidth= 2
style.icon.radius= 16
```

## Tüm Distance Tipleri

| Distance Type| String Kısaltması|
|-------------------|------------|
|  centimeters      | 'cm'|
|  meter            | 'm'|
|  kilometer        | 'km'|
|  groundmile       | 'mile'|
|  seamile          | 'nm'|
|  inch             | 'in'|
|  feet             | 'ft'|
|  yard             | 'yd'|

## Tüm Açı Tipleri

| Angle Type| String Kısaltması|
|-------------------|------------|
|  degree      | '°'|
|  radyan      | 'rad'|
|  grad        | 'g'|
|  angularmil  | 'mil'|
|  minuteofarc | '\''|
|  secondofarc | '"'|
