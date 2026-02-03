# Track

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Tanım

Büyük nokta setlerini performanslı bir şekilde çizebileceğiniz, performansta kayıp olmadan anlık olarak nokta setine ekleme ya da silme yapabileceğiniz yapıdır. `POINT` nesne tipi olarak deniz seviyesinde çizilir.

**Kullanım Alanları:**
- Nokta bulutu çizimi
- Uçak kuyruk izleri gösterimi
- Büyük veri setlerinin gerçek zamanlı görselleştirmesi

Track metodları `myGlobe.Track` sınıfından çağrılır.
