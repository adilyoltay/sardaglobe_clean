# Kamera İleri ve Geri Alma İşlemleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Fare'nin tekerleğiyle yapılan yaklaşma ve uzaklaşma animasyonu dışındaki tüm navigasyon işlemleri için kamera önceki veya sonraki konumlarına alınabilir.

|Metod                                                               |                                                               Açıklama|
|---------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------|
|[api_GoToPreviousPosition()](#kamerayı-Önceki-konumuna-alma)|Kamerayı bir önceki konumuna alır.|
|[api_GoToNextPosition()](#kamerayı-sonraki-konumuna-alma)|Kamerayı bir sonraki konumuna alır.|
|[api_IsPreviousPositionAvailable()](#kamerayı-Önceki-konumuna-alma-kontrolü)|Kameranın bir önceki konumuna alınıp alınamayacağını kontrol eder.|
|[api_IsNextPositionAvailable()](#kamerayı-sonraki-konumuna-alma-kontrolü)|Kameranın bir sonraki konumuna alınıp alınamayacağını kontrol eder.|
