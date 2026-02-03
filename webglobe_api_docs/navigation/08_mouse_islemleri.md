# Mouse İşlemleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

|Metod                                                               |                                                               Açıklama|
|---------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------|
|[api_SetMouseEvents("event",callback)](screen/?id=mouse-events)|Kullanıcı tarafından(fare ve dokunma) `down`, `move`, `up`, `click`, `dblclick`,`zoomstart`, `zoomend` event'lerine atanacak callback'leri Globe'a verir.|
|[api_ClearMouseEvents()](screen/?id=mouse-events)|Kullanıcı tarafından atanan mouse eventlerinin hepsini siler.|
|[api_GetMouseDeg()](howto/?id=farenin-enlem-ve-boylam-değerini-alma)|Farenin canvas üzerindeki pixel koordinatlarından haritaya düşen noktasının enlem ve boylam değerlerini derece cinsinden verir.|
