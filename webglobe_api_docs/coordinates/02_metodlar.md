# Koordinat Metodları

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

| Metod                                 | Açıklama                                                                                                  |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| `SetType(coordinateType)`        | Koordinat gösterim tipini değiştirir. Varsayılan koordinat gösterim tipi `geo`dur. |
| `GetType()`        | Mevcut koordinat gösterim tipini döndürür. |
| `SetGeoDigits(numdigits)`        | Derece cinsinden yapılan koordinat gösterimlerinde basamak sayısını belirtir. Varsayılan değeri 8'dir. |
| `SetDMSDigits(numdigits)`        | DMS cinsinden yapılan gösterimlerde saniye basamak sayısını belirtir. Varsayılan değeri 4'tür. |
| `SetDMDigits(numdigits)`        | DM cinsinden yapılan gösterimlerde dakika basamak sayısını belirtir. Varsayılan değeri 5'tir. |
| `SetProjDigits(numdigits)`        | UTM cinsinden yapılan gösterimlerde basamak sayısını belirtir. Varsayılan değeri 2'dir. |
| `SetGeoPrefix(prefixLat, prefixLong)`        | Derece cinsinden gösterimlerde prefix değerlerini değiştirir. |
| `SetDMSPrefix(prePlusLat,preMinusLat,prePlusLong,preMinusLong)`        | DMS ve DM cinsinden gösterimlerde prefix değerlerini değiştirir. |
| `SetMGRSPrefix(prefix)`        | MGRS cinsinden gösterimlerde prefix değerini değiştirir. |
| `SetProjPrefix(prefixY, prefixX)`        | UTM cinsinden gösterimlerde prefix değerlerini değiştirir. |
| `SetGeoRefPrefix(prefix)`        | GeoRef cinsinden gösterimlerde prefix değerini değiştirir. |
| `SetDegreeChars(degStr, minStr, secondStr)`        | DMS ve DM cinsinden gösterimlerde derece, dakika ve saniye karakterlerini değiştirir. |
| `SetProjString(projString)`        | Proj koordinat tipinde kullanılacak string cümlesini ayarlar. |
| `SetSeparator(separatorStr)`        | Enlem ve boylam değerleri arasındaki ayırıcıyı değiştirir. |
| `SetOrder(latitudeFirst)`        | Enlem mi boylam mı önce yazacağını ayarlar. |
| `GetCoordStr(long, lat)`        | Verilen noktayı aktif koordinat tipine göre string olarak verir. |

## Örnekler

### Koordinat Gösterim Tipini Değiştirme
```javascript  
myGlobe.Coordinates.SetType("dms")
```

### GEO Prefix Değerlerini Değiştirme
```javascript  
myGlobe.Coordinates.SetGeoPrefix('Lat : ', 'Long : ')
```

### DMS ve DM Prefix Değerlerini Değiştirme
```javascript  
myGlobe.Coordinates.SetDMSPrefix('North : ', 'South : ', 'East : ', 'West : ')
```

### Aktif Koordinat Tipinin Değerini Alma
```javascript  
const str = myGlobe.Coordinates.GetCoordStr(32,40)
console.log(str)
```
