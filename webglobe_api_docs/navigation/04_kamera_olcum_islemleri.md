# Kamera Ölçüm İşlemleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

|Metod                                                               |                                                               Açıklama|
|---------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------|
|[api_GetCurrentLOD()](#yakınlaştırma-seviyesitam-sayı)|Kameranın bulunduğu uzaklık seviyesini tam sayı olarak verir.|
|[api_GetCurrentLODWithDecimal()](#yakınlaştırma-seviyesi)|Kameranın bulunduğu uzaklık seviyesini verir.|
|[api_OrbitDistance()](#kameranın-yörüngeye-olan-uzaklığı)|Kameranın yörüngeye olan uzaklığını verir(metre). Küre geometri tipinde baz alınır.|
|[api_CamZ()](#kameranın-deniz-seviyesinden-yüksekliği)|Kameranın deniz seviyesinden yüksekliğini verir(metre). Küre geometri tipinde baz alınır.|
|[api_Altitude()](#kameranın-yeryüzüne-olan-İzdüşümü)|Kameranın yeryüzüne olan izdüşüm uzaklığını verir(metre). Küre geometri tipinde baz alınır.|
|[api_GetCameraDist()](#kameranın-yeryüzüne-olan-yüksekliği)|Kameranın bulunduğu yüksekliği verir(metre). Küre geometri tipinde baz alınır.|
