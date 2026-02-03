# Tanım

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Vektör katmanlarında kullanılan `CSLayersTypes.CS_OBJECT_ARRAY` katman tipinin küre bazlı çalışan versiyonudur.

Küre bazlı çizim desteği, `style` parametrelerine macro getirebilme imkanı, vektör katmanlarında yavaş olan `api_UpdateLayerData` metodunun küre bazlıda çok hızlı bir şekilde çalışması, seçim(selection) kısımlarındaki performans artışı, bazı çizim kısımlarının daha hızlı ve performanslı bir şekilde çalışabilmesi amacıyla getirildi.

Şu an, nesne tipi `CSObjectTypes.POINT` olan nesneler için küre bazlı çizim desteği sağlanıyor.

Küre bazlı Object Array çizim metodları `myGlobe.ObjectArray` sınıfından çağrılarak kullanılır.
