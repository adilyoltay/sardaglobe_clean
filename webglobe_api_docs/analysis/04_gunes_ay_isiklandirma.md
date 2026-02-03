# Güneş, Ay'ın Konumu ve Işıklandırılması

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Web Küre Güneş ve Ay'ın pozisyonlarına göre ışıklandırılabilir.

## Metodlar

| Metod                                 | Açıklama                                                                                                  |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| `api_CalcSunMoon(date, isUTC, applySunLight, applyMoonLight)`| Verilen tarihe göre Güneş ve Ay'ın pozisyonlarını bulur ve ışıklandırma yapar. |

## Dönen Nesne Yapısı

```javascript
{
  moon:{
    lng,
    lat,
    illumPercentage,
    phase,
    phaseValue,
    moonPath,
    moonTimeCoords
  },
  sun: {
    lng,
    lat,
    sunPath,
    sunTimeCoords
  }
}
```

## Dönen Değerler

|değer| açıklama|
|--------------------|--------------------------------|
|`lng`               |Güneş veya Ay'ın konumu|
|`lat`               |Güneş veya Ay'ın konumu|
|`sunPath`           |Güneş'in 24 saatlik yolu|
|`sunTimeCoords`     |Güneş'in saatlik konumları|
|`illumPercentage`   |Ay'ın ışık yüzdesi |
|`phaseValue`        |Ay'ın faz değeri |
|`phase`             |Ay'ın evresi|
|`moonPath`          |Ay'ın 24 saatlik yolu|
|`moonTimeCoords`    |Ay'ın saatlik konumları|

## Örnek Kullanım

```javascript
const date= new Date()  // şu anki tarih
const isUTC = false // Local zamana göre tarih seçildi
const applySunLight = true
const applyMoonLight = true

const sunMoonObj = myGlobe.api_CalcSunMoon(date, isUTC, applySunLight, applyMoonLight)
console.log(sunMoonObj.sun.lng, sunMoonObj.sun.lat)
console.log(sunMoonObj.moon.lng, sunMoonObj.moon.lat)
console.log(sunMoonObj.moon.phase, sunMoonObj.moon.illumPercentage)
```
