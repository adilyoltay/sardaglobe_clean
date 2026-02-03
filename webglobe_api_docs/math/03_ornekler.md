# Örnekler

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Alan Bulma Örneği

```javascript  
const coords = [32,40,34,42,35,41]
const totalArea = myGlobe.Math.CalcPolyAreaM2(coords, true, 100)
```

>[!WARNING]
>is3D parametresi true olduğunda, stepLimit değeri arttıkça hesaplama süresi uzar!
