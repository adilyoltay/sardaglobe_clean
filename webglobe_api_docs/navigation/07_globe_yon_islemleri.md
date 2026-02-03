# Globe Yön İşlemleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

|Metod                                                               |                                                               Açıklama|
|---------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------|
|[api_NorthAngleDeg()](#kuzey-açısını-al)|Kuzey Açısını derece cinsinden verir. Küre geometri tipinde baz alınır.|
|[api_TurnToNorth(degree)](/navigation/?id=kuzeye-animasyon-yaparak-dön)|Haritayı kuzeye animasyonlu biçimde döndürür. açı parametresi verildiğinde verilen açıya gider. Verilmediği takdirde kuzeye döner. Küre geometri tipinde baz alınır.|
|[api_TurnToNorthDirect(degree)](/navigation/?id=kuzeye-animasyon-yapmadan-dön)| Haritayı kuzeye animasyonsuz biçimde döndürür. Küre geometri tipinde baz alınır. |
|[api_SetNorthAngle(degree)](#kuzey-açısını-ayarlama)| Kameranın kuzey açısını değiştirir. Küre geometri tipinde baz alınır.|
|[api_SetLockNorth(boolean)](#kuzey-açısını-kitleme)| `boolean` değeri `true` verildiğinde kameranın kuzey açısını 0 dereceye çekip, bu değerin değiştirilmesini engelller. `boolean` değeri `false` olduğunda ise kuzey açısı değiştirilebilir. Varsayılan değeri `false`tur. Küre geometri tipinde baz alınır. |
|[api_SetTiltAngle(angle)](#bakış-açısını-değiştirme)|Kameranın bakış açısını değiştirir. Küre geometri tipinde baz alınır.|
|[api_Set2DMode(boolean)](#haritayı-2d-moda-alma)| `boolean` değeri `true` verildiğinde kameranın bakış açısını 0 dereceye çekip, bu değerin değiştirilmesini engelleyerek haritayı 2D moda alır. `boolean` değeri `false` olduğunda bakış açısı değeri değiştirilebilir ve harita 3D moda geçer. Varsayılan değeri `false`tur. Küre geometri tipinde baz alınır.|
|[api_GetMagneticNorthAngle(long, lat, date)](/navigation/?id=manyetik-kuzey-açısını-al)|Derece cinsinden verilen enlem ve boylam değerlerine göre o noktanın manyetik kuzey açısı değerini verir. `date` değeri verildiğinde noktanın verilen tarihteki manyetik kuzey açısı değerini, verilmezse o anki manyetik kuzey açısı değerini verir. Kullanıcıya derece cinsinden değer döner.|
