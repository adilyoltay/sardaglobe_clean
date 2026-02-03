# Kamera Yönetimi

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Kamera yönetimi metodları ile kameranın istenilen şekilde kontrol edilmesi sağlanır. Kamera yönetimi devralındığında Globe'un diğer navigasyon işlemleri olan fare ile navigasyon ve nokta veya bölgeye gitme **kamera yönetimi bırakılmadan yapılamaz**. Kamera yönetimi için bir **veri akışına** ihtiyaç vardır, bu veri akışı ile kamera istenilen şekilde yönetilebilir. Veri akışı örneğin bir İHA konum bilgileri olabilir veya kullanıcı yön tuşları ile kendisi veri akışı sağlayabilir.

## Kamera Callback

Kamera callback, kamera yönetimini devralmak için gerekli nesnedir. Kamera callback [api_SetCameraCallBack](/navigasyon/?id=kamera-callback-atama)  metodu ile Globe'a atanır ve Globe her çizim döngüsünde  kamera callback'in `SetPosition` metodunu kullanarak **veri akışından** gelen kamera bilgilerini günceller. Böylece başka bir kaynakten gelen veri veya kullanıcıdan gelen verilerle kamera yönetimi yapılır.


Kamera callback bir sınıf nesnesi veya sadece bir nesne olabilir.Bu nesnede [api_SetCameraPos](/navigasyon/?id=kamera-pozisyonu-güncelle) fonksiyonu için gerekli parametreler özellik olarak bu nesne içerisinde tutulabilir. Oluşturulan callback nesnesinde `SetPosition` **metodu olmalıdır** ve içerisinde api_SetCameraPos **api metodunu çağırmalıdır**.


>[!SCODE]
> Örneğin bir nesne oluşturalım ve api_SetCameraPos için gerekli özellikleri oluşturalım sonra SetPosition metodunu yazalım


```javascript
var cameraCallBack = {
  CamLong: 0,
  CamLat: 0,
  CamDist: 0,
  NoCamDist: 0,
  CamNorthAngleDeg: 0,
  CamTiltAng: 0,
  CamRollAng: 0,
  SetPosition: function() {
    myGlobe.api_SetCameraPos(
      this.CamLong,
      this.CamLat,
      this.CamDist,
      this.CamNorthAngleDeg,
      this.CamTiltAng,
      this.CamRollAng
    )
  }
}
```

Kamera callback oluştuktan sonra camera callback Globe'a atanır ve kamera yönetimi devralınmış olur. Bundan sonra veri akışı ile callback özellikleri güncellendiğinde Globe her çizim döngüsünde kamera bilgilerini güncelleyecektir.

```javascript
myGlobe.api_SetCameraCallBack(cameraCallBack)
```  

Kamera yönetimini bırakmak için [api_LeaveCamera](/navigasyon/?id=kamera-yönetimini-globea-aktarma) metodu kullanılır. Kamera callback Globe'tan silinir ve varsayılan navigasyon moduna geçer.

```javascript
myGlobe.api_LeaveCamera(5000)
```

## Kamera Pozisyonu Güncelle

>[!CODE|label:Kamera pozisyonunu günceller|]
><p class="title csPM">api_SetCameraPos(long, lat, distMeter, northAngleDeg, tiltAngDeg, rollAngDeg)</p>

| Parametre       | Açıklama              |
| --------------- | --------------------- |
| `long`          | Boylam değeri        |
| `lat`           | Enlem değeri         |
| `distMeter`          | Yükseklik(metre)            |
| `northAngleDeg` | XY ekseninde çevirme(derece cinsinden) |
| `tiltAngDeg`       | Eğim(derece cinsinden)             |
| `rollAngDeg`       | Yana yatıklık(derece cinsinden)        |

>[!SCODE]

```javascript
myGlobe.api_SetCameraPos(42, 23, 24, 2, 45, 45)
```

## Kamera Callback Atama

>[!CODE|label:Oluşturulan kamera callback nesnesini küreye atayan metodtur, bu metod ile birlikte kamera yönetimi devralınmış olur|]
><p class="title csPM">api_SetCameraCallBack(callbackObj)</p>

| Parametre     | Açıklama                                |
| ------------- | --------------------------------------- |
| `callbackObj` | Kamera için set edilecek user callback. |


## Kamera Yönetimini Globe'a Aktarma

>[!CODE|label:Kamera yönetimini küreye aktarır|]
><p class="title csPM">api_LeaveCamera(externalDistMeter)</p>

| Parametre           | Açıklama                                |
| ------------------- | --------------------------------------- |
| `externalDistMeter` | Kameranın yüksekliği. (metre) |
