# Tanım

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

Isı haritası, bir olgunun büyüklüğünü renklerle ifade eden bir veri görselleştirme tekniğidir.

Isı haritaları, küre üzerinde, hem rasterize hem de shader bazlı gösterilebilir. 2 çizim tekniğinin de birtakım avantaj ve dezavantajları mevcuttur.

## Çizim Teknikleri Karşılaştırması

| Özellik | Rasterize Bazlı | Shader Bazlı |
|---------|----------------|--------------|
| Çözünürlük | Düşük | Yüksek |
| Precalculate Hızı | Yavaş | Hızlı |
| Performans (çok nokta) | İyi | Değişken |
| Dinamik Stil Değişikliği | Hayır | Evet |
| Metre Bazlı Gösterim | İyi | Sınırlı |
| Piksel Bazlı Gösterim | Hayır | Evet |

## Shader Bazlı Isı Haritası Tipleri

| Tip | Açıklama |
|-----|----------|
| `CSHeatmapTypes.POINT` | Nokta bazlı ısı haritası |
| `CSHeatmapTypes.SQUARE_GRID` | Kare grid bazlı ısı haritası |
| `CSHeatmapTypes.HEXAGON_GRID` | Altıgen grid bazlı ısı haritası |

## Kullanım Önerileri

- **Çok fazla nokta + Metre bazlı:** Rasterize tekniği önerilir
- **Dinamik stil değişikliği:** Shader bazlı POINT tipi önerilir
- **Grid görünümü:** SQUARE_GRID veya HEXAGON_GRID önerilir
- **Cluster ile birlikte:** POINT tipi + piksel bazlı gösterim önerilir
