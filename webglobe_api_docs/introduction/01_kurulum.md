# Kurulum

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Pirireis Bilişim tarafından geliştirilen CAS Web, web ortamında, üç boyutlu küre ve iki boyutlu düzlem üzerinde çalışan coğrafi analiz sistemidir. Kurulum için aşağıdaki adımlar izlenmelidir.

## NPM Paketi

CAS Web geliştirme apisi aşağıdaki paket ile yüklenir:

```
npm i @pirireis/webglobe
```

## Proje için Gerekli Dosyalar

```
./src  
- index.html  
-/node-modules/
--@pirireis/webglobe/
--- webglobe.js  (legacy: main.js)

./anotherdirectory/
- webglobeserver.dll  
- CSHostID.csytk
```

## Gerekli Server Bileşenleri

- Mesh adresleri için Globe server yüklü olmalıdır.
- Raster adresleri için WMS servisi gereklidir.
- Adreslerin çalışması için cross-domain izinlerini kontrol ediniz.
- webglobeserver.dll GlobeApi modülüne ulaşmak için gerekli dll dosyası.
- CSHostID.csytk GlobeApi modülü için gerekli izin dosyası, kurulum esnasında tarafımızdan size sağlanacaktır.

## Gerekli Html Elementleri

```html
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf8" />
</head>
<body>
  <div id="parentDiv">
    <canvas id="globe"></canvas>
  </div>
</body>
</html>
```
