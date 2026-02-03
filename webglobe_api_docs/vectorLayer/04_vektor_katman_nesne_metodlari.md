# Vektör Katman Nesne Metodları

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

| Metod                        | Açıklama     |
| -----------------------------| ----------------|
| `setDrawMode(drawMode, immediateUpdate)`|  Vektör katman nesnelerinin çizim modunu değiştirir. `drawMode` parametresi `CSDrawMode.NORMAL` ve `CSDrawMode.SELECTED` değerlerinden birini alabilir. Nesnenin `drawMode` değeri `CSDrawMode.NORMAL` olduğunda, nesneler, vektör katmanlarında tanımlanan `style` parametresinden, `drawMode` değeri `CSDrawMode.SELECTED` olduğunda ise `selectedStyle` parametresinden çizim stillerini alarak çizilirler. Nesnelerin varsayılan çizim modu `CSDrawMode.NORMAL`dir. `immediateUpdate` parametresi `boolean` değer alır, `true` verildiğinde çizim modu değiştirilen nesne `drawMode` değerine göre çizim stilini `style` ya da `selectedStyle`dan alarak anında uygulanır ve `api_CheckLayerDrawModeChanges(layer)` metodunun kullanılmasına gerek kalmaz. Örnekler için [bakınız](/howto/?id=vektör-katmanı-ve-object-array-nesne-seçim-Örnekleri)|
| `getDrawMode()`| Vektör katman nesnelerinin çizim modu(`drawMode`) değerini döndürür. Örnekler için [bakınız](/howto/?id=vektör-katmanı-ve-object-array-nesne-seçim-Örnekleri)|
