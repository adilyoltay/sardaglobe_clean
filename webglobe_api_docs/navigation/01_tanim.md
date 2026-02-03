# Tanım

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Navigasyon metodları ile Globe'un kamera yönetimi alınabilir, kamera yüksekliği, uzaklık bilgileri, yön bilgileri elde edilebilir. Kullanıcı isterse varsayılan metodlar ile belli noktaya veya bölgeye gidebilir veya kamera yönetimini devralıp istediği animasyon, uçuş simülatörü, belli bir rotayı gezdirme, belli bir cismi takip etme, gerektiğinde interaktif olarak kamerayı yönlendirme gibi işlemleri yapabilir.
