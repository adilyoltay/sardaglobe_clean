# Tanım

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Katmanlar, nesneleri toplu şekilde belirli kısıtlara(uzaklığa, bölgeye) göre nesne tiplerini göstermeyi sağlayan yapılardır.

Katman yapıları farklı türlerde olabilir ve farklı veri tiplerini destekleyebilirler.

Katman yapıları `point`, `line` ve `polygon` nesne tiplerini destekler. Ayrıca `CS_OBJECT_ARRAY` katman tipi `shape`, `arcArea` ve `OBJECT_3D` nesne tipini de destekler. `OBJECT_3D` nesne tipi verilerek `point`, `line`, `polygon`, `ellipse`, `circle`, `rectangle`, `rectangle_bbox`,`arcArea`, `orbit`, `polyArc`, `corridor` ve `track` gibi nesneler 3 boyutlu olarak çizilebilir.
