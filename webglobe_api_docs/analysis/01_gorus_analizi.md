# Görüş Analizi

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Verilen gözlem noktasından belli bir yarıçapta gözlemcinin görüş analizini `PNG` formatında döndürür.

Görüş analizi hesabı `scanRadius`, `startAng`, `stopAng`, `imageSize` ve `observerCoords` (gözlemci sayısı) değerlerine göre **1 saniye ile 10 dakika** arası sürebilir.

## Kullanım

```javascript
const params = {
  "observerCoords":[long, lat],
  "observerHeight":5,
  "receiverHeight":0.1,
  "scanRadius":1000,
  "startAng":0,
  "stopAng":360,
  "imageSize":256,
  "visColor":"#00ff00",
  "threshold" : 8.0,
  "callback": function(img, imgLimit){
    if(img & imgLimit){
      console.log("Visibility Analysis result img: ", img)
      console.log("Visibility Analysis result img limit: ", imgLimit)
    } else {
      console.log("err or cancel");
    }
  }
}

const reqid = myGlobe.api_VisibilityAnalysis(params,10000)

// if you want to abort request:
if(reqid){
  myGlobe.api_CancelVisibilityAnalysis(reqid)
}
```

## Parametreler

| Parametre                    |    Açıklama |
|------------------------------|-------------|
|  `observerCoords`            |  Gözlemci noktasının pozisyonudur. [boylam, enlem] derece cinsinden. |
|  `observerHeight`            |  Gözlemci noktasının yüksekliği. `0.1m`den küçük olamaz. |
|  `receiverHeight`            |  Gözlem yapılacak alanın taban yüksekliği. `0.1m`den küçük olamaz. |
|  `scanRadius`                |  Gözlem yapılacak alanın yarıçapı. `100m`den düşük olamaz. |
|  `startAng`                  |  Daire diliminin başlangıç açısı. `0-360` derece. |
|  `stopAng`                   |  Daire diliminin bitiş açısı. `0-360` derece. |
|  `imageSize`                 |  Sonuç resmin boyutu. `64-2048` arası değer alır. |
|  `visColor`                  |  Görünen kısımların rengi. |
|  `callback(img,imgLimit)`    |  Analiz sonunda çağrılır. |
