# Tanım

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Koordinat sistemleri; bir noktanın Dünya üzerindeki yerinin eksenlerle (enlemler-boylamlar) yaptığı açı cinsinden veya bu eksenlere uzaklığının metre cinsinden belirtildiği sistemlerdir.

Kullanıcılar `myGlobe.Coordinates` sınıfının metodlarını kullanarak istedikleri işlemleri yapabilir ve koordinat tipine göre durum çubuğunda koordinat gösterimi yapabilirler.

## Koordinat Gösterim Tipleri

| Coordinates Types| Açıklama|
|-------------------|------------|
|  geo      | Koordinat gösterimi derece cinsinden yapılır. |
|  dms      | Koordinat gösterimi derece, dakika, saniye (DEGREE MINUTE SECOND) cinsinden yapılır. |
|  dm       | Koordinat gösterimi derece, ondalık dakika (DEGREE, DECIMAL MINUTE) cinsinden yapılır. |
|  mgrs     | Koordinat gösterimi mgrs (Military Grid Reference System) cinsinden yapılır. |
|  proj     | UTM(Universal Transform Mercator) projeksiyonu için destekleniyor. |
|  georef   | Koordinat gösterimi georef (World Geographic Reference System) cinsinden yapılır. |
