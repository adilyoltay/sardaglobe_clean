# Çizim ve Araç İşlemleri

## Proje Hedefi (Genel)

Bu doküman, `AGENTS.md` hedefleriyle uyumludur: **öncelik** `globe-web-html/libs/webglobe.js` API/behavior parity; **ikincil hedef** Google Earth benzeri core globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation vb.) yakınsamaktır. Mimari dönüşümler parity'yi bozmadan ilerler.

## Çizim İşlemleri

| Metod | Açıklama |
|-------|----------|
| `api_FPS()` | Son bir saniyede çizilen frame sayısını döndürür |
| `api_SetMaxFPS(FPS)` | Maksimum FPS değerini ayarlar |
| `api_ResetMaxFPS()` | FPS'i varsayılan değere (50) çeker |
| `api_SetFog(boolean)` | Sisi açıp kapatır |
| `api_SetModelWireFrameMode(boolean)` | 3D modelleri wireframe modunda gösterir |
| `api_SetWireFrameMode(boolean)` | Globe wireframe modunu açıp kapatır |
| `api_UseTextureBuffer(boolean)` | Texture buffer kullanımını ayarlar |
| `api_SetGlobalSymbolAndTextScale(symbolScale, textScale)` | Sembol ve yazı ölçeğini ayarlar |
| `api_SetSpaceColor(color)` | Uzay arka plan rengini değiştirir |
| `api_SetGlobeColor(color)` | Globe temel rengini değiştirir |
| `api_ShowGrid(boolean, gridMode, fontSize)` | WGS84/UTM ızgarasını gösterir |
| `api_DrawBaseGlobeColor(boolean)` | Base globe rengini çizip çizmemeyi kontrol eder |

## Araç İşlemleri (UI Bileşenleri)

| Metod | Açıklama |
|-------|----------|
| `api_ShowStatusBar(boolean)` | Durum çubuğunu gösterir/gizler |
| `api_ShowCompass(boolean)` | Pusulayı gösterir/gizler |
| `api_ShowOverview(boolean)` | Önizleme panelini gösterir/gizler |
| `api_ShowScaleBar(boolean)` | Ölçek barını gösterir/gizler |
| `api_ShowDebug(boolean)` | Debug panelini gösterir/gizler |
| `api_SetCompass(position, offset, size)` | Pusula konumunu ayarlar |
| `api_SetOverview(position, offset, size)` | Önizleme paneli konumunu ayarlar |
| `api_SetScaleBar(position, offset)` | Ölçek barı konumunu ayarlar |
| `api_OnScaleBarClick(callback)` | Ölçek bar tıklama callback'i atar |
| `api_SetScalebarImage(imgUrl)` | Ölçek bar resmini değiştirir |
| `api_GetScaleBarPos()` | Ölçek barı konumunu döndürür |

## Pozisyon Tipleri (CSCorner)

| Değer | Açıklama |
|-------|----------|
| `LEFT_TOP` | Sol üst |
| `RIGHT_TOP` | Sağ üst |
| `LEFT_BOTTOM` | Sol alt |
| `RIGHT_BOTTOM` | Sağ alt |
| `MID_TOP` | Üst orta (sadece ölçek bar) |
| `MID_BOTTOM` | Alt orta (sadece ölçek bar) |

## Grid Mode Tipleri (CSGridMode)

| Değer | Açıklama |
|-------|----------|
| `GEO_WGS84` | WGS84 koordinat sistemi ızgarası |
| `UTM` | UTM koordinat sistemi ızgarası |
