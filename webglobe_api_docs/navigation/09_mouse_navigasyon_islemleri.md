# Mouse Navigasyon İşlemleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

|Metod                                                               |                                                               Açıklama|
|---------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------|
|[api_SetNavigationSpeed(speed)](#navigasyon-hızı-ayarlama)|Fare Navigasyon hızı, girilen değer ile hızlandırılabilir veya yavaşlatılabilir.|
|[api_GetNavigationSpeed()](#navigasyon-hızı-ayarlama)|Fare Navigasyon hızını verir.|
|[api_SetMouseWheelDirection(boolean)](howto/?id=yakınlaşma-yönü-belirleme)|Fare tekerleğinin hangi yönde yakınlaşma uzaklaşma yapacağını ayarlar, default false değerindedir, true değeri ile ters yapılır.|
|[api_SetMouseWheelMode(boolean)](/howto/?id=yaklaşma-uzaklaşma-modu-belirleme)|`true` verildiğinde, kamera, tekerlek ile yapılan yaklaşma ve uzaklaşmalarda farenin gösterdiği yere doğru gider, `false` verildiğinde ekran merkezinden yaklaşır ya da uzaklaşır. Varsayılan değeri `false`tur.|
