# Dil Ayarları

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Tanım

Dil ayarı varsayılan olarak Türkçe'dir.

İngilizce ve Türkçe dil desteği bulunmaktadır.

## Desteklenen Diller

|Dil|Kodu|
|---|----|
|Türkçe| `tr`|
|İngilizce| `eng`|

## Dili Değiştirme

Dili değiştirmek için istenilen dil kodu ile aşağıdaki api komutu kullanılır:

```javascript
myGlobe.api_SetLang("tr")
```
