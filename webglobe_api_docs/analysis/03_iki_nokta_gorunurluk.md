# İki Nokta Arasındaki Görünürlük Analizi

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

`api_LineOfSight(long1, lat1, long2, lat2, observerH, recieverH, level, timeout, threshold)`

Enlem ve boylam değerleri derece cinsinden verilen 2 nokta arasında hat boyu görünürlük analizi yapar.

## Dönen Değer

`[[distance,long,lat,Z,visibility],[distance,long,lat,Z,visibility], ... ]` şeklinde bir dizi döndürür.

## Parametreler

- `long1, lat1`: Birinci nokta (derece)
- `long2, lat2`: İkinci nokta (derece)
- `observerH`: Gözlemci noktasının yüksekliği (metre)
- `recieverH`: Gözlem yapılacak alanın taban yüksekliği (metre)
- `level`: Çözünürlük (`0-15` arası)
- `timeout`: İstek timeout süresi (varsayılan 15000 ms)
- `threshold`: Eşik değeri (varsayılan 8)

## Örnek Kullanım

```javascript
const obsH = 1.75
const recH = 0.01

const response = myGlobe.api_LineOfSight(30.41, 36.24, 30.43, 36.28,
  obsH, recH, 8, 6000, 0.25)
response.then((result)=> {
  console.log(result)
})
```
