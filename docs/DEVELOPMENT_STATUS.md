# Native Globe - Development Status

**Son Güncelleme:** 2026-02-04

---

## Mimari Not

Bu repo'da iki hat var:

| Hat | Açıklama | Durum |
|-----|----------|-------|
| `src/` | Yeni modüler engine (CMake build) | **Aktif geliştirme** |
| `src_backup/` | Eski kapsamlı parity kod tabanı | **Referans/arşiv** |

Eski plan dokümanları (`MASTER_DEVELOPMENT_PLAN.md`, `API_PORT_REVIEW_PROMPT.md`) **src_backup/** hattına göre yazılmıştı. Yeni `src/` modüler mimarisinde:

- **FAZ 1.3 (Scheduler/Decode Uyumu):** N/A - zaten tek pipeline var
- **FAZ 2.1/2.2 (State Machine Tekleştirme):** N/A - zaten tek `TileState` enum

---

## Tamamlanan Fazlar (src/ hattı)

| Faz | Açıklama | Tarih |
|-----|----------|-------|
| FAZ 1.2 | Komşu LOD Fark Sınırı (maxNeighborDelta=1) | 2026-02-04 |
| FAZ 6.1 | DEM Seam Fix (edgeCoarserMask + edge equalization) | 2026-02-04 |

---

## Aktif Faz: FAZ 4 (Layer & Query)

### Hedef API'ler

| API | Öncelik | Durum |
|-----|---------|-------|
| `AddRaster` / `DeleteRaster` | P0 | Pending |
| `SetRasterService` | P0 | Pending |
| `GetGeoFromScreenPoint` | P0 | Pending |
| `GetScreenPointFromGeo` | P0 | Pending |
| `QueryByScreen` | P0 | Pending |
| `QueryByBbox` | P1 | Pending |

### Referans Dosyalar (src_backup/)

- `layer_manager.h/cpp` - Layer yönetimi
- `globe_api.h/cpp` - API yüzeyi
- `tile_lod_selector.h/cpp` - SSE LOD (zaten src/'da LodSelector olarak var)

---

## Sonraki Fazlar

| Faz | Açıklama |
|-----|----------|
| FAZ 5 | Draw & Style API'leri |
| FAZ 6.2-6.4 | DEM async callback, elevation profile |
| FAZ 7 | Polish & Release |
