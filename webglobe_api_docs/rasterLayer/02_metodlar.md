# Raster Katmanı Metodları

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Temel Metodlar

| Metod | Açıklama |
|-------|----------|
| `api_AddRaster(rasterObj, beforeObject)` | Raster ekler. `beforeObject` parametresi ile çizim sırası belirlenir |
| `api_SetRasterService(index, rasterObj, clearBBOX)` | Index'i verilen raster'ı günceller |
| `api_DeleteRaster(index)` | Index'i verilen raster'ı siler |
| `api_SetMaxOpenRasterCount(value)` | Globe üzerinde en fazla kaç raster aktif olabileceğini ayarlar. Varsayılan: 2 |
| `api_SetRasterONOFF(index, isON)` | Raster'ın aktifliğini değiştirir |
| `api_GetRasterONOFF(index)` | Raster'ın aktif olup olmadığını döndürür |
| `api_RasterCount()` | Raster sayısını döndürür |
| `api_GetRaster(index)` | Index'i verilen raster'ı döndürür |
| `api_GetRasterById(id)` | ID'ye göre raster'ı döndürür |
| `api_GetNewRasterId()` | Otomatik raster ID üretir |

## Opacity ve LOD Metodları

| Metod | Açıklama |
|-------|----------|
| `api_SetRasterOpacity(index, value)` | Raster'ın opacity değerini değiştirir |
| `api_GetRasterOpacity(index)` | Raster'ın opacity değerini döndürür |
| `api_SetRasterLodPriority(boolean)` | LOD önceliğini ayarlar. `true` ise kaliteye bakmadan indirir |

## Timeout ve Renk Metodları

| Metod | Açıklama |
|-------|----------|
| `api_SetRasterTimeOut(ms)` | Timeout süresini değiştirir. Varsayılan: 5000ms |
| `api_ReTryAtRasterTimeout(boolean, callback)` | Timeout'a düşen tile'lar için yeniden indirme ayarı |
| `api_SetGlobalRasterColor(color)` | Tüm raster katmanlarının rengini değiştirir |

## Toplu İşlemler

| Metod | Açıklama |
|-------|----------|
| `api_GetTotalLayersAsJSON()` | Tüm raster ve vektör katmanları JSON olarak döndürür |
| `api_AddTotalLayers(json)` | JSON ile toplu raster ve vektör katmanları ekler |
