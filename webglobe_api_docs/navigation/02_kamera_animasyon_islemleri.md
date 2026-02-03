# Kamera Animasyon İşlemleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

|Metod                                                               |                                                               Açıklama|
|---------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------|
|[api_FlyToRegion(long1, lat1, long2, lat2, scale)](#bölgeye-git) | Kamerayı haritadaki enlem ve boylam değerleri ile belirtilen bölgeye animasyon yaparak götürür.|
|[api_FlyToRegionDirect(long1, lat1, long2, lat2, scale)](#anında-bölgeye-git)| Kamera, haritadaki enlem ve boylam değerleri ile belirtilen bölgeye yaklaşma animasyonu yapmadan anında gider.|
|[api_FlyToPoint(longdegree, latdegree, distMeter, northAngleDeg, tiltAngleDeg)](#noktaya-git)| Kamerayı haritadaki enlem ve boylam değerleri ile belirtilen noktaya animasyon yaparak götürür.|
|[api_FlyToPointDirect(longdegree, latdegree, distMeter, northAngleDeg, tiltAngleDeg)](#anında-noktaya-git)| Kamera, haritadaki enlem ve boylam değerleri ile belirtilen noktaya yaklaşma animasyonu yapmadan anında gider.|
|[api_ZoomToPaperScale(scale)](#yörünge-noktasına-git)| Kamera, yörünge noktasına verilen `scale` değerine göre gider.|
|[api_SetContinuousRotation(boolean)](#devaml%c4%b1-animasyon)|Varsayılan olarak false değerindedir, `boolean` değeri false ise sürükle bırak animasyonu yavaşlayarak durur, `true` ise sürükle bırak işlemi devamlı halde olur. Küre geometri tipinde baz alınır. |
