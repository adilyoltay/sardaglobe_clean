# Units (Birimler)

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Tanım

Birimler, fiziksel niceliklerin ölçülmesinde, ifade edilmesinde ve aynı tür niceliklerin birbiriyle karşılaştırılmasında kullanılan uluslararası standart büyüklüklerdir.

Birim metodları `myGlobe.Units` sınıfından çağrılır.

## Desteklenen Birim Kategorileri

| Kategori | Varsayılan | Açıklama |
|----------|------------|----------|
| Distance (Mesafe) | `meter` | Uzunluk ölçümleri |
| Area (Alan) | `metersquare` | Alan ölçümleri |
| Altitude (İrtifa) | `meter` | Yükseklik ölçümleri |
| Angle (Açı) | `degree` | Açı ölçümleri |
| Volume (Hacim) | `cubicmeter` | Hacim ölçümleri |
