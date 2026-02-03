# Koordinat Dönüşümleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Koordinat Dönüşümlerinde Kullanılabilecek WebKüre Api'leri

| Metod                                 | Açıklama                                                                                                  |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| `api_GeoToUTM(datumType, long, lat)`        | Coğrafi koordinat sisteminden UTM koordinat sistemine dönüşüm yapar. `{x, y, zone}` döndürür. |
| `api_UTMToGeo(datumType, zone, x, y)`        | UTM koordinat sisteminden coğrafi koordinat sistemine dönüşüm yapar. `{long, lat}` döndürür. |
| `api_GetUTMZone(long, lat)`        | Verilen noktanın zone bilgisini döndürür. |
| `api_GeoToGeoRef(long, lat)`        | Coğrafi koordinat sisteminden GeoRef sistemine dönüşüm yapar. |
| `api_GeoRefToGeo(geoRef)`        | GeoRef sisteminden coğrafi koordinat sistemine dönüşüm yapar. |
| `api_GeoToDMS(long, lat)`        | Dereceden DMS'e dönüşüm yapar. |
| `api_DMSToGeo(dmsObj)`        | DMS'ten dereceye dönüşüm yapar. |

## Desteklenen Datum Tipleri

| Datum Type| Açıklama|
|-------------------|------------|
|  `WGS84`       | Dünya Jeodezik Sistemi-1984. Türkiye'de 2002 yılından itibaren kullanılmaktadır. |
|  `ED50`        | Avrupa Datumu-1950. Türkiye'de 2001 yılına kadar kullanılmıştır.|

```javascript
const CSDatumTypes = {
  WGS84,
  ED50
}
```

## Proj String Cümle Örnekleri

| Datum Type| Kullanım|
|-------------------|------------|
|  `WGS84`       | "+proj=utm +k=0.9996 +zone=36 +towgs84=0.00000,0.00000,0.00000 +ellps=WGS84 +no_defs" |
|  `WGS72`        | "+proj=utm +k=0.9996 +zone=36 +towgs84=0.00000,0.00000,0.00000 +ellps=WGS72 +no_defs"|
|  `ED50`        | "+proj=utm +k=0.9996 +zone=36 +towgs84=-87.00000,-96.00000,-120.00000 +ellps=intl +no_defs"|

## DMS'ten Dereceye Dönüşüm Örnekleri

### Örnek 1 (Mevcut Dil Türkçe - Doğru Kullanım)
```javascript
const dmsObj = {
                 long : {deg:32, min:6, sec:36, direction:'D'},
                 lat : {deg:42, min:21, sec:36.36, direction:'G'}
               }
const longLat = myGlobe.api_DMSToGeo(dmsObj)
```

### Örnek 2 (Mevcut Dil İngilizce - Doğru Kullanım)
```javascript
const dmsObj = {
                 long : {deg:32, min:6, sec:36, direction:'E'},
                 lat : {deg:42, min:21, sec:36.36, direction:'S'}
               }
const longLat = myGlobe.api_DMSToGeo(dmsObj)
```
