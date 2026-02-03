# Screen (Ekran İşlemleri)

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Tanım

Globe üzerinde **ekran**, **mouse**, **klavye** ve **obje tespiti** ile ilgili API metodlarını içerir.

## Metod Seviyeleri

Metodlar iki seviyede çalışır:
- **Globe bazlı:** `myGlobe.api_*` şeklinde çağrılır
- **GlobeManager bazlı:** `GlobeManager.api_*` şeklinde çağrılır

## Kategoriler

1. **Koordinat İşlemleri** - Ekran ↔ Coğrafi koordinat dönüşümleri
2. **Mouse İşlemleri** - Mouse event yönetimi ve zoom davranışları
3. **Klavye İşlemleri** - Klavye ile bbox çizimi ve navigasyon
4. **Çizim İşlemleri** - FPS, fog, wireframe ve stil ayarları
5. **Araç İşlemleri** - UI bileşenleri (pusula, ölçek barı vb.)
6. **Obje Tespit İşlemleri** - Nesne sorgulama
