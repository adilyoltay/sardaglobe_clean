# Yükseklik Profil Alma

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

`api_FindProfile(coords, level, timeout, threshold)`

Verilen koordinat dizisinin profilini `[[distance,long,lat,Z],[distance,long,lat,Z], ... ]` dizisi şeklinde döndürür.

## Parametreler

- `coords`: Koordinat dizisi
- `level`: Profil noktalarının çözünürlüğü (`0-15` arası)
- `timeout`: İstek timeout süresi (varsayılan 15000 ms)
- `threshold`: Eşik değeri (varsayılan 8)

## Örnek Kullanım

```javascript
const coords = [long1, lat1, long2, lat2, long3, lat3]
const level = 10
const timeout = 6000
const threshold = 0.25

const response = myGlobe.api_FindProfile(coords, level, timeout, threshold)
response.then((result)=> {
  console.log(result)
})
```
