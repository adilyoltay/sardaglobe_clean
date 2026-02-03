# Kamera Kontrol İşlemleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

|Metod                                                               |                                                               Açıklama|
|---------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------|
|[api_SetCameraCallBack(callbackObj)](/navigation/?id=kamera-callback)|Kamera görünümünü ayarlar. Sadece uçuş modu için kullanılır. Küre geometri tipinde baz alınır.|
|[api_SetCameraPos(long, lat, dist, northAngleDeg, tiltAng, rollAng)](#kamera-pozisyonu-güncelle)|Kamera pozisyonunu günceller. Sadece uçuş modu için kullanılır. Küre geometri tipinde baz alınır.|
|[api_LeaveCamera(externalDistMeter)](/navigation/?id=kamera-yönetimini-globe39a-aktarma)|Kamera yönetimini Globe'a aktarır. Sadece uçuş modu için kullanılır. Küre geometri tipinde baz alınır.|
|[api_GetCurrentLookInfo()](#kamera-bilgilerini-elde-etme)|Kameranın baktığı ekranın orta noktasının koordinatlarını ve o noktaya olan bakış açılarını verir.|
|[api_GetDirectPosNatural()](/navigation/?id=kamerayı-bakış-bilgisi-ile-yönlendirme)| Kameranın o anki bakış bilgilerini verir. Kameranın coğrafi bilgilerinden bağımsızdır. |
|[api_SetDirectPosNatural(naturalpos)](/navigation/?id=kamerayı-bakış-bilgisi-ile-yönlendirme)| Kameranın verilen bakış bilgilerine birebir aynı şekilde konumlanmasını sağlar. |
|[api_ZoomToLOD(zoomtype)](#lod-seviyesine-yaklaşma-uzaklaşma)| Verilen tipte veya LOD seviyesine zoom yapar. `zoomtype`: `zoomin` ise yaklaşır, `zoomout` ise uzaklaşır, `LODnumber` Integer tipinde ve 0'dan 25'e kadar değer alır, her tipte ve verilen değerde LOD değerine zoom yapar. |
|[api_SetCameraPosChanged(ms, callback)](/navigation/?id=kameranın-pozisyonunun-değişmesi)| Bir `ms`(milisaniye) ve `callback` verilerek kameranın pozisyonu değiştiğinde istenen işlemler yapılabilir. `callback`in son tetiklenişinden itibaren geçen süre verilen `ms` değerinden büyük ya da eşitse `callback` tetiklenir.|
