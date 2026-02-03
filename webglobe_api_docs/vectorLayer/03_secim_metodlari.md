# Seçim Metodları

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

| Metod                        | Açıklama     |
| -----------------------------| ----------------|
| `api_CheckLayerDrawModeChanges(layer)`| Çizim modu değişen nesnelerin durumunu küre üzerinde uygular. 2 tip çizim modu vardır. `CSDrawMode.NORMAL` ve `CSDrawMode.SELECTED`. Seçim(`CSDrawMode.SELECTED`) modunda olan nesneler çizim stillerini `selectedStyle`dan, seçim modunda olmayan(`CSDrawMode.NORMAL`) nesneler ise `style`dan alırlar ve ona göre çizilirler. Örnek kullanım için [bakınız](/howto/?id=ctrl-klavye-tuşu-eventi-atandıktan-sonra-Çizilen-region-İçine-düşen-nesneleri-seçme-Örneği) |
| `api_SetLayerSelectedList(layer, styleOrMVTXYZName, fidList)`| `Tek boyutlu dizi` olarak `fidList`te verilen değerlere sahip olan nesneleri bulup çizim modunu `CSDrawMode.SELECTED` yaparak `selectedStyle` değerlerine göre çizilmesini sağlar. |
| `api_GetLayerSelectedList(layer,styleOrMVTXYZName)`     | Verilen katmanın seçim modunda olanların listesini `dizi` olarak döndürür. |
| `api_SetLayerSelectionFlash(layer, flashIcon, flashLabels, flashGeo, flashShape)`| Seçim modunda(`CSDrawMode.SELECTED`) olan nesnelerin yanıp sönme değerlerini değiştirip küre üzerinde uygular. |
| `api_SetLayerSelectionOpacity(layer, opacity)`       | Katmanın seçim stillerinin saydamlık değerini değiştirir ve seçim modunda(`CSDrawMode.SELECTED`) olan nesneler, verilen `opacity` değeriyle ekran üzerinde gösterilir. `0<=opacity<=1` |
| `api_GetDefaultLayerSelectedStyle()`             | Varsayılan seçim stilini verir.|
