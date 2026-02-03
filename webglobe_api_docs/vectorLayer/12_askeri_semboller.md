# Askeri Sembolleri Ekleme

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Askeri sembolleri küre üzerinde gösterebilmek için ilk olarak kullanılacak askeri sembol kütüphanesi import edilmeli ve daha sonra `api_SetMilSymbol` metoduyla küreye eklenmelidir.

## Askeri Sembol Kütüphanesini Ekleme

```javascript
import * as ms from 'milsymbol'

myGlobe.api_SetMilSymbol(ms) //artık askeri sembol kütüphanesi kullanılabilir
```

Askeri sembol kütüphanesi kullanıma hazır hale getirildikten sonra vektör katmanı olarak eklenebilir. `objectType`ı `POINT` olan `CS_OBJECT_ARRAY` vektör katman tipi için desteklenir.

## Örnek Kullanım

```javascript

var style = myGlobe.api_GetDefaultLayerStyle()
style.iconType = CSIconTypes.MAP // milIcon ekleyebilmek için icon tipi MAP olmalı
style.milIcon = ['${symbol}.${app6DCode}'] //attribs kısmından okunacak sembol koduna erişmek için gerekli key

var CSPointArrLayer={
  id : myGlobe.api_GetNewLayerId(),
  displayName: 'denemeLayer',
  layerType  :myGlobe.api_LAYER_TYPE_CS_OBJECT_ARRAY(),
  style      : style,
  data : [
    {
      coords: [32, 40, 34, 42, 35, 41],
      coordsZ: [0, 0, 0],
      attribs: [{ symbol: { app6DCode: '10030100001101250000' } }, { symbol: { app6DCode: 'sfgpewrh--mt' } }, { symbol: { app6DCode: 'SFG*UCDSC-*****' } }]
    }
  ],
  objectType : CSObjectTypes.POINT,
  bbox       : null,
  startLod   : 2,
  endLod     : 22
}

myGlobe.api_AddLayer(CSPointArrLayer)

```

>[!SCODE|label:Askeri Sembol Ekleme Resim|]

<img height="500" width="700" src="assets/img/milIcon.jpg" data-origin="milIcon.jpg" alt="Askeri Semboller">

>[!WARNING]
> Askeri sembol gösterebilmek için style'daki `iconType` değeri `MAP`, `icon` kısmındaki name ve mapName değerleri ise '' olmalıdır.

## milIconParams Parametreleri

| Parametre| Açıklama|
|-------------------|------------|
|  `borderWidth`    | Askeri sembollerin çizgi kalınlığıdır. Varsayılan değeri `3`tür.|
|  `filled`         | Askeri sembollerin içinin dolu mu yoksa boş mu indirileceğini belirler. Varsayılan değeri `true`dur.|
|  `colorMode`      | Askeri sembollerin indirileceği rengi belirler. (`Light`, `Medium`, `Dark`) Varsayılan değeri `Light`tır.|
|  `extraParams`    | Farklı boyut ve stillerde önceden indirilecek semboller.|

## Askeri Sembol extraParams Yapısı

```javascript
layerStyle.milIconParams.extraParams = [
  { size: 12, borderWidth: [1.2, 3], filled: CSMilIconFillledTypes.FILL, colorMode: ['Dark']  },
  { size: 18, borderWidth: [1.8, 3], filled: CSMilIconFillledTypes.UNFILL, colorMode: ['Light']  },
  { size: 24, borderWidth: [2.4, 3], filled: CSMilIconFillledTypes.BOTH, colorMode: ['Dark', 'Light']  },
]
```

## extraParams filled Tipleri

| `filled` Type| Açıklama|
|-------------------|------------|
|  FILL           | Askeri semboller sadece içi dolu olarak indirilir.|
|  UNFILL         | Askeri semboller sadece içi boş olarak indirilir.|
|  BOTH           | Askeri semboller hem içi dolu hem de boş olarak indirilir.|

## Askeri Sembol colorMode Kullanım Örneği

```javascript
import * as ms from 'milsymbol'

//colorMode oluşturma
ms.setColorMode('myColorMode', ms.ColorMode('rgb(255,255,255)' , 'rgb(0,0,255)', 'rgb(255,0,0)', 'rgb(255,255,0)', 'rgb(0,255,255)'))

// vektör katmanı ekleme
const style = myGlobe.api_GetDefaultLayerStyle()
style.iconType = CSIconTypes.MAP
style.milIcon = ['${symbol}.${app6DCode}']
style.milIconParams = {
  borderWidth: 3,
  filled: true,
  colorMode: 'myColorMode',
  extraParams: []
}

const CSPointArrLayer={
  id : myGlobe.api_GetNewLayerId(),
  displayName: 'CsPointLayer',
  layerType  :myGlobe.api_LAYER_TYPE_CS_OBJECT_ARRAY(),
  style,
  data : [
    {
      coords: [32, 40, 34, 42, 35, 41],
      coordsZ: [0, 0, 0],
      attribs: [{ symbol: { app6DCode: '10030100001101250000' } }, { symbol: { app6DCode: '10000100000000000000' } }, { symbol: { app6DCode: '10061100001101000000' } }]
    }
  ],
  objectType : CSObjectTypes.POINT,
  bbox       : null,
  startLod   : 2,
  endLod     : 22
}

myGlobe.api_AddLayer(CSPointArrLayer)

```

>[!SCODE|label:Askeri Sembol colorMode|]

<img height="500" width="700" src="assets/img/milSymbolColorMode.jpg" data-origin="milSymbolColorMode.jpg" alt="Askeri Sembol colorMode">
