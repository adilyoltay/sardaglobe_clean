# Örnekler

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Bölgeye Git

>[!CODE|label:Kamerayı enlem ve boylam değerleri ile belirtilen bölgeye götürür|]

<p class="title csPM">myGlobe.api_FlyToRegion(long1, lat1, long2, lat2, scale)


| Parametre | Açıklama                                                    |
| --------- | ----------------------------------------------------------- |
| `long1`   | Bölgenin sol alt noktasının derece cinsinden boylam değeri  |
| `lat1`    | Bölgenin sol alt noktasının derece cinsinden enlem değeri   |
| `long2`   | Bölgenin sağ üst noktasının derece cinsinden boylam değeri  |
| `lat2`    | Bölgenin sağ üst noktasının derece cinsinden enlem değeri   |
| `scale`   | Bölgeye yakınlaşma ölçeği, küre geometri tipinde baz alınır |

>[!SCODE]

```javascript
myGlobe.api_FlyToRegion(26, 36, 45, 42, 1);
```

## Anında Bölgeye Git

>[!CODE|label:Kamera haritadaki enlem ve boylam değerleri ile belirtilen bölgeye yaklaşma animasyonu yapmadan anında gider|]

<p class="title csPM">myGlobe.api_FlyToRegionDirect(long1, lat1, long2, lat2, scale)


| Parametre | Açıklama                                                    |
| --------- | ----------------------------------------------------------- |
| `long1`   | Bölgenin sol alt noktasının derece cinsinden boylam değeri  |
| `lat1`    | Bölgenin sol alt noktasının derece cinsinden enlem değeri   |
| `long2`   | Bölgenin sağ üst noktasının derece cinsinden boylam değeri  |
| `lat2`    | Bölgenin sağ üst noktasının derece cinsinden enlem değeri   |
| `scale`   | Bölgeye yakınlaşma ölçeği, küre geometri tipinde baz alınır |

>[!SCODE]

```javascript
myGlobe.api_FlyToRegionDirect(26, 36, 45, 42, 1);
```

## Devamlı Animasyon

>[!CODE|label: true değeri ile sürükle bırak işlemi devamlı halde olurken false değeri verildiğinde sürükle bırak animasyonu yavaşlayarak durur Varsayılan değeri false |]

<p class="title csPM">api_SetContinuousRotation(boolean)

 | Parametre | Açıklama                                                    |
 | --------- | ----------------------------------------------------------- |
 | `boolean`   | `true` ise devamlı animasyon yapar durur , `false` ise sürükle bırak animasyonu yavaşlayarak durur. |

## Noktaya Git

>[!CODE|label:Kamerayı enlem ve boylam değerleri ile belirtilen noktaya götürür |]

<p class="title csPM">myGlobe.api_FlyToPoint(longdegree, latdegree, distMeter, northAngleDeg, tiltAngleDeg)


| Parametre       | Açıklama                                       |
| --------------- | ---------------------------------------------- |
| `longdegree`    | Hedef noktanın derece cinsinden boylam değeri  |
| `latdegree`     | Hedef noktanın derece cinsinden enlem değeri   |
| `distMeter`     | Küre geometri tipi için metre cinsinden yükseklik değeri, düzlem geometri tipi için noktanın etrafında oluşturulan karenin bir kenarının metre cinsinden değeri  |
| `northAngleDeg` | Kuzey açısı, küre geometri tipinde baz alınır  |
| `tiltAngleDeg`  | Eğim açısı, küre geometri tipinde baz alınır   |

>[!SCODE]

```javascript
myGlobe.api_FlyToPoint(23.555555, 33.34555, 10000, 0, 45)
```

## Anında Noktaya Git

>[!CODE|label:Kamerayı enlem ve boylam değerleri ile belirtilen noktaya götürür|]

<p class="title csPM">myGlobe.api_FlyToPointDirect(longdegree, latdegree, distMeter, northAngleDeg, tiltAngleDeg)


| Parametre       | Açıklama                                       |
| --------------- | ---------------------------------------------- |
| `longdegree`    | Hedef noktanın derece cinsinden boylam değeri  |
| `latdegree`     | Hedef noktanın derece cinsinden enlem değeri   |
| `distMeter`     | Küre geometri tipi için metre cinsinden yükseklik değeri, düzlem geometri tipi için noktanın etrafında oluşturulan karenin bir kenarının metre cinsinden değeri  |
| `northAngleDeg` | Kuzey açısı, küre geometri tipinde baz alınır  |
| `tiltAngleDeg`  | Eğim açısı, küre geometri tipinde baz alınır   |

>[!SCODE]

```javascript
myGlobe.api_FlyToPointDirect(23.555555, 33.34555, 10000, 45, 30)
```

## Yörünge Noktasına Git

>[!CODE|label:Kamera yörünge noktasına verilen scale değerine göre gider|]

<p class="title csPM">myGlobe.api_ZoomToPaperScale(scale)


| Parametre       | Açıklama                                       |
| --------------- | ---------------------------------------------- |
| `scale`         | Bölgeye yakınlaşma ölçeği                      |

>[!SCODE]

```javascript
myGlobe.api_ZoomToPaperScale(5000)
```



## Yakınlaştırma Seviyesi(Tam Sayı)

>[!CODE|label:Kameranın bulunduğu uzaklık seviyesini tam sayı olarak verir|]
><p class="title csPM">api_GetCurrentLOD()</p>

>[!SCODE]

```javascript
myGlobe.api_GetCurrentLOD()
```

## Yakınlaştırma Seviyesi

>[!CODE|label:Kameranın bulunduğu uzaklık seviyesini verir|]
><p class="title csPM">api_GetCurrentLODWithDecimal()</p>

>[!SCODE]

```javascript
myGlobe.api_GetCurrentLODWithDecimal()
```

## Kuzey Açısını Al

>[!CODE|label:Kuzey Açısını derece cinsinden verir|]
><p class="title csPM">api_NorthAngleDeg()</p>


>[!SCODE]


```javascript
myGlobe.api_NorthAngleDeg()
```

## Kuzeye Animasyon Yaparak Dön

>[!CODE|label:Kamerayı kuzeye animasyon yaparak döndürür|]
><p class="title csPM">api_TurntoNorth(degree)</p>


>[!SCODE]


```javascript
myGlobe.api_TurntoNorth(30)
```

## Kuzeye Animasyon Yapmadan Dön

>[!CODE|label:Kamerayı kuzeye animasyon yapmadan döndürür|]
><p class="title csPM">api_TurnToNorthDirect(degree)</p>


>[!SCODE]


```javascript
myGlobe.api_TurnToNorthDirect(45)
```

## Kuzey Açısını Ayarlama

>[!CODE|label:Kameranın kuzey açısını ayarlar |]
><p class="title csPM">api_SetNorthAngle(degree)</p>


>[!SCODE]


```javascript
myGlobe.api_SetNorthAngle(90)
```

## Kuzey Açısını Kitleme

>[!CODE|label:Kameranın kuzey açısını 0 dereceye çekerek kuzey açısının değiştirilmesini engeller|]
><p class="title csPM">api_SetLockNorth(boolean)</p>

| Parametre | Açıklama                                                            |
| --------- | ------------------------------------------------------------------- |
| `boolean`    | Kameranın kuzey açısının değiştirilip değiştirilemeyeceğini belirleyen parametredir. `true` verildiğinde kuzey açısı değiştirilebilirken `false` verildiğinde değiştirilemez. Varsayılan değeri `false`tur. |

>[!SCODE]


```javascript
myGlobe.api_SetLockNorth(true)
```


## Bakış Açısını Değiştirme

>[!CODE|label:Kameranın bakış açısını verilen angle değerine göre değiştirir|]
><p class="title csPM">api_SetTiltAngle(angle)</p>

| Parametre | Açıklama                                                            |
| --------- | ------------------------------------------------------------------- |
| `angle`    | Kameranın bakış açısının derece cinsinden değeridir. `angle` değeri 0 ile 90 arasında verilmelidir.   |

>[!SCODE]


```javascript
myGlobe.api_SetTiltAngle(30)
```

## Haritayı 2D Moda Alma

>[!CODE|label:Kameranın bakış açısını 0 dereceye çeker ve bu değerin değiştirilmesini engelleyerek haritayı 2D moda alır|]
><p class="title csPM">api_Set2DMode(boolean)</p>

| Parametre | Açıklama                                                            |
| --------- | ------------------------------------------------------------------- |
| `boolean`    | Kameranın hangi modta kullanılacağını belirleyen parametredir. `true` verildiğinde 2D, `false` verildiğinde 3D modta çalışır. Varsayılan değeri `false`tur. |

>[!SCODE]


```javascript
myGlobe.api_Set2DMode(true)
```

## Manyetik Kuzey Açısını Al

>[!CODE|label:Manyetik kuzey açısını derece cinsinden verir|]
><p class="title csPM">api_GetMagneticNorthAngle(long, lat, date)</p>


>[!SCODE]


```javascript
const magneticNorthValue = myGlobe.api_GetMagneticNorthAngle(32, 40)
```

## Navigasyon Hızı Ayarlama

>[!CODE|label:Fare Navigasyon hızı girilen değer ile hızlandırılabilir veya yavaşlatılabilir|]
><p class="title csPM">api_SetNavigationSpeed(speed)</p>

| Parametre | Açıklama                                                            |
| --------- | ------------------------------------------------------------------- |
| `speed`    | `speed=> 0.1 ve speed=< 3.0` arasında bir değer olmalıdır. |

## Kamerayı Önceki Konumuna Alma

Kullanıcı kendisinin tasarladığı bir düzenleme aracıyla kamerayı önceki konumuna alabilir. Varsayılan bir düzenleme aracı yoktur.

>[!CODE|label: Kamerayı Önceki Konumuna Alma|]
><p class="title csPM">api_GoToPreviousPosition()</p>

## Kamerayı Önceki Konumuna Alma Kontrolü

Kameranın bir önceki konumuna alınıp alınamayacağını kontrol eder. Dönen değer `true` ise kamera bir önceki konumuna alınabilir `false` ise alınamaz. Kullanıcıların ilk olarak `api_IsPreviousPositionAvailable()` metodunu kullanarak kameranın önceki konumuna alınıp alınamayacağı kontrolü yapmaları ve dönen değer `true` ise `api_GoToPreviousPosition()` metodunu kullanarak kamerayı önceki konumuna almaları daha doğru bir yöntemdir.

>[!CODE|label: Kamerayı Önceki Konumuna Alma Kontrolü|]
><p class="title csPM">api_IsPreviousPositionAvailable()</p>

## Kamerayı Sonraki Konumuna Alma

Kullanıcı kendisinin tasarladığı bir düzenleme aracıyla kamerayı sonraki konumuna alabilir. Varsayılan bir düzenleme aracı yoktur.

>[!CODE|label: Kamerayı Sonraki Konumuna Alma|]
><p class="title csPM">api_GoToNextPosition()</p>

## Kamerayı Sonraki Konumuna Alma Kontrolü

Kameranın bir sonraki konumuna alınıp alınamayacağını kontrol eder. Dönen değer `true` ise kamera bir sonraki konumuna alınabilir `false` ise alınamaz. Kullanıcıların ilk olarak `api_IsNextPositionAvailable()` metodunu kullanarak kameranın sonraki konumuna alınıp alınamayacağı kontrolü yapmaları ve dönen değer `true` ise `api_GoToNextPosition()` metodunu kullanarak kamerayı sonraki konumuna almaları daha doğru bir yöntemdir.

>[!CODE|label: Kamerayı Sonraki Konumuna Alma Kontrolü|]
><p class="title csPM">api_IsNextPositionAvailable()</p>

## Kameranın Yörüngeye Olan Uzaklığı

>[!CODE|label:Kameranın yörüngeye olan metre uzaklığını verir|]
><p class="title csPM">api_OrbitDistance()</p>

>[!SCODE]

```javascript
myGlobe.api_OrbitDistance()
```

## Kameranın Deniz Seviyesinden Yüksekliği

>[!CODE|label:Kameranın deniz seviyesinden metre yüksekliğini verir|]
><p class="title csPM">api_CamZ()</p>

>[!SCODE]

```javascript
myGlobe.api_CamZ()
```

## Kameranın Yeryüzüne Olan İzdüşümü

>[!CODE|label:Kameranın yeryüzüne olan izdüşüm metre uzaklığını verir|]
><p class="title csPM">api_Altitude()</p>

>[!SCODE]

```javascript
myGlobe.api_Altitude()
```

## Kameranın Yeryüzüne Olan Yüksekliği

>[!CODE|label:Kameranın yükseliğini metre cinsinden verir|]
><p class="title csPM">api_GetCameraDist()</p>

>[!SCODE]

```javascript
myGlobe.api_GetCameraDist()
```

## Kameranın Maksimum Uzaklık Seviyesini Ayarlama

>[!CODE|label:Kameranın maksimum uzaklık seviyesini ayarlar|]
><p class="title csPM">api_SetMaxNavigationLOD(lod)</p>

>[!SCODE]

```javascript
myGlobe.api_SetMaxNavigationLOD(15)
```

## Kameranın Minimum Uzaklık Seviyesini Ayarlama

>[!CODE|label:Kameranın minimum uzaklık seviyesini ayarlar|]
><p class="title csPM">api_SetMinNavigationLOD(lod)</p>

>[!SCODE]

```javascript
myGlobe.api_SetMinNavigationLOD(8)
```

## Kameranın Uzaklık Seviyesini Ayarlama

>[!CODE|label:Kameranın uzaklık seviyesini ayarlar|]
><p class="title csPM">api_SetMinNavigationLOD(lod)</p>

>[!SCODE]

```javascript
myGlobe.api_SetNavigationLOD(8)
```

## Maksimum Uzaklığı Ayarlama

>[!CODE|label:Kürede kameranın yeryüzüne olan maksimum izdüşüm düzlemde ise noktanın etrafında oluşturulan karenin bir kenarının maksimum uzaklığını ayarlar|]
><p class="title csPM">api_SetMaxNavigationDist(dist)</p>

>[!SCODE]

```javascript
myGlobe.api_SetMaxNavigationDist(10000000) // metre
```

## Minimum Uzaklığı Ayarlama

>[!CODE|label:Kürede kameranın yeryüzüne olan minimum izdüşüm düzlemde ise noktanın etrafında oluşturulan karenin bir kenarının minimum uzaklığını ayarlar|]
><p class="title csPM">api_SetMinNavigationDist(dist)</p>

>[!SCODE]

```javascript
myGlobe.api_SetMinNavigationDist(1000) // metre
```

## Uzaklığı Ayarlama

>[!CODE|label:Kürede kameranın yeryüzüne olan izdüşüm düzlemde ise noktanın etrafında oluşturulan karenin bir kenarının uzaklığını ayarlar|]
><p class="title csPM">api_SetNavigationDist(dist)</p>

>[!SCODE]

```javascript
myGlobe.api_SetNavigationDist(50000) // metre
```

## Haritanın Görünür Ekran Genişlik Uzaklığını Ayarlama

>[!CODE|label:Haritanın görünür ekran genişlik uzaklığını ayarlar|]
><p class="title csPM">api_SetScreenWidth(scrWidth, lock)</p>

>[!SCODE]

```javascript
myGlobe.api_SetScreenWidth(1000, true) // metre
```

## Haritanın Görünür Ekran Genişliği İle Maksimum ve Minimum Uzaklık Kitlemelerini İptal Etme

>[!CODE|label:Haritanın görünür ekran genişliği ile maksimum ve minimum uzaklık kitlemelerini iptal eder|]
><p class="title csPM">api_CancelScreenWidthAndMinMaxLOD()</p>

>[!SCODE]

```javascript
myGlobe.api_CancelScreenWidthAndMinMaxLOD()
```

## Kamera bilgilerini elde etme

Kameranın bulunduğu bir andaki enlem, boylam, mesafe, eğim açısı ve kuzey açısı değerini json objesi olarak verir. Bu değerler `api_FlyToPoint` metoduna verildiğinde kameranın daha önceki bulunduğu duruma gider.

>[!CODE|label: Kamera Bilgileri|]
><p class="title csPM">api_GetCurrentLookInfo()</p>

| Parametre           | Açıklama                                |
| ------------------- | --------------------------------------- |
| `CenterLong` | Merkez noktanın boylam değeri(derece) |
| `CenterLat` | Merkez noktanın enlem değeri(derece) |
| `Distance` | Küre geometri tipinde kameranın metre cinsinden merkeze olan uzaklığı, düzlem geometri tipinde ise haritanın görünür alanının metre cinsinden uzunluğu |
| `NorthAng` | Kuzey açısı(derece) |
| `Tilt` | Eğim açışı(derece) |

>[!SCODE]

```javascript
const cameraInfo = myGlobe.api_GetCurrentLookInfo()

myGlobe.api_FlyToPoint(cameraInfo.OrbitLong,cameraInfo.OrbitLat,cameraInfo.OrbitDist,
cameraInfo.NorthAng,cameraInfo.Tilt)

```

## Kamerayı bakış bilgisi ile yönlendirme

```javascript

const getCamNaturalPos = myGlobe.api_GetDirectPosNatural()

myGlobe.api_SetDirectPosNatural(getCamNaturalPos)

```

## LOD seviyesine yaklaşma uzaklaşma

Lod seviyeleri bazında yaklaşma uzaklaşma için aşağıdaki komut kullanılır. Lod seviyelerine gider.

>[!CODE|label:LOD seviyesine yaklaşma|]
><p class="title csPM">api_ZoomToLOD(zoomtype)</p>

| Parametre           | Açıklama                                |
| ------------------- | --------------------------------------- |
| `zoomin`|  Yaklaşma|
| `zoomout`|  Uzaklaşma|
|  LOD number  | Integer tipinde ve 0 ile 25 arası değerleri alır.  |


```javascript

myGlobe.api_ZoomToLOD("zoomin") // yaklaşma

myGlobe.api_ZoomToLOD("zoomout") // uzaklaşma

myGlobe.api_ZoomToLOD(11) // verilen lod değerine gitme


```

## Kameranın Pozisyonunun Değişmesi

Kameranın pozisyonu değiştiğinde kullanıcının istediği işlemleri yapabilmesi için aşağıdaki komut kullanılır.

>[!CODE|label:Kamera Pozisyonunun Değişmesi|]
><p class="title csPM">api_SetCameraPosChanged(ms, callback)</p>

| Parametre           | Açıklama                                |
| ------------------- | --------------------------------------- |
| `ms`| Milisaniye cinsinden süredir. `callback`in son tetiklenişinden itibaren geçen süre verilen `ms` değerinden büyük ya da eşitse `callback` tetiklenir. |
| `callback`| Kameranın pozisyonu değiştiğinde istenen işlemlerin yapılabilmesi için gerekli callback |


```javascript
const ms = 1000
const cameraPosCallback = function () {
    console.log('Camera Position Changed')
}
myGlobe.api_SetCameraPosChanged(ms, cameraPosCallback)
```
