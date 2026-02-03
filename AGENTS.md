# AGENTS.md — Native Globe Reference Index

Bu dosya, projedeki ana dokümanları ve kaynak referanslarını tek noktadan listeler.
Her faz tamamlandığında `docs/API_PORT_REVIEW_PROMPT.md` içindeki **Faz Tamamlama Günlüğü**
ve **Güncel Durum Snapshot** bölümleri güncellenmelidir.

## Ana Master Kural
**Temel hedef (ikili):**
1) **API/Behavior parity:** `globe-web-html/libs/webglobe.js` davranışları ve API yüzeyiyle **tam parity** sağlamak.  
2) **Core mimari hedef:** Google Earth benzeri bir çekirdek globe mimarisine (tile pyramid, SSE LOD, tile state machine, async elevation, vb.) **yakınsamak**.

> Çatışma olursa **API/behavior parity önceliklidir**, mimari dönüşüm parity’yi bozmayacak şekilde yapılır.

> **NOT:** 2026-01-29 itibariyle ana referans `webglobe/main.js` yerine `globe-web-html/libs/webglobe.js` olarak değiştirilmiştir.

## Navigasyon Parity Kuralı (İstisna)
Navigasyon davranışlarında (mouse/keyboard pan-orbit-zoom-tilt) **JS yerine Google Earth parity** esas alınacaktır.  
JS ile çelişki varsa, **navigasyon için Google Earth davranışı önceliklidir**.

## Dokümanlar
- `docs/MASTER_DEVELOPMENT_PLAN.md` — **ANA GELİŞTİRME PLANI** (7 faz, 3 hafta) ✅
- `docs/JS_CPP_PARITY_ANALYSIS.md` — JS↔C++ tutarsızlık analizi.
- `docs/API_PORT_REVIEW_PROMPT.md` — Review checklist + faz planı + durum snapshot'ı.
- `docs/GOOGLE_EARTH_REWRITE_BLUEPRINT.md` — Google Earth mimari blueprint (agresif çıkarım).
- `docs/GOOGLE_EARTH_REWRITE_PLAN.md` — Blueprint’i native_globe’a uygulama planı (fazlar).
- `README.md` — Proje genel açıklaması (varsa build/run notları).
- `webglobe_api_docs/README.md` — WebKüre API modüler dokümantasyon kök dizini.
- `webglobe_api_docs/WebKure_API_Documentation.md` — Ana API dokümantasyonu (JS parity referansı).

## Kaynak Referansları (Güncel)
- `globe-web-html/libs/webglobe.js` — **ANA JS KAYNAK** (minified, 2.2MB), davranış parity referansı.
- `webglobe_deobfuscated_v2/**` — **Güncel** deobfuscate edilmiş JS kaynak (webglobe.js'den).
- `webglobe_deobfuscated_v2/webglobe_beautified.js` — Beautified tam kaynak (67,818 satır).
- `webglobe_deobfuscated_v2/constants/globe_constants.js` — Ta sabitleri (GLOBE_*, vb.).
- `webglobe_deobfuscated_v2/api_list.txt` — API isimleri (358 kayıt).

## Google Earth Tersine Mühendislik Referansları (Mimari Hedef)
- `docs/GOOGLE_EARTH_INTEGRATION_REPORT.md` — **TEK KAPSAMLI RAPOR** (800+ satır, 2026-01-30)
- `~/Desktop/google_earth/` — Kaynak dizin (WASM, WAT, reconstructed headers)

**Mimari Uyum İçin Öncelikli Yapılar:**
- TileKey (QuadKey, Parent/Child/Neighbor navigation)
- SSE-based LOD selection (Screen-Space Error)
- Skirt generation (LOD seam prevention)
- Tile state machine, Async elevation query

## Eski Kaynak Referansları (Arşiv)
- `api_list.json` — Eski API listesi (365 kayıt, main.js'den).
- `webglobe/main.js` — Eski JS kaynak (artık kullanılmıyor).
- `webglobe_deobfuscated/**` — Eski deobfuscated kaynak (main.js'den, artık kullanılmıyor).

## C++ API Katmanı
- `src/globe_api.h` — Override edilen API listesi.
- `src/globe_api.cpp` — Gerçek implementasyonlar.
- `src/globe_api_generated.h` — Generated API deklarasyonları.
- `src/globe_api_generated.cpp` — Stub implementasyonlar (`Value::Null()`).
- `src/value.h` — `Value` tipleri ve JSON-benzeri yapı.

## Engine / Core
- `src/globe_engine.h` — Engine API, sabitler (GLOBE_RADIUS*, vb.).
- `src/globe_engine.cpp` — Kamera, animasyon, raster, query, dönüşümler.
- `src/layer_manager.h` — Layer API, feature yapılandırmaları.
- `src/layer_manager.cpp` — Query, geometry yardımcıları (PointInPolygon vb.).

## Faz Güncelleme Kuralı
Her faz tamamlandığında aşağıdaki güncellemeler yapılır:
1) `docs/API_PORT_REVIEW_PROMPT.md` → **Faz Tamamlama Günlüğü** işaretlenir.
2) `docs/API_PORT_REVIEW_PROMPT.md` → **Güncel Durum Snapshot** metrikleri güncellenir.
3) Gerekirse **Mevcut İmplementasyonlar** listesi revize edilir.

## Blueprint/Plan Zorunluluğu
- `docs/GOOGLE_EARTH_REWRITE_BLUEPRINT.md` ve `docs/GOOGLE_EARTH_REWRITE_PLAN.md` **yürütme kuralıdır**.
- Yeni mimari değişiklikler bu iki dokümana dayanmalı ve plan fazlarıyla uyumlu olmalıdır.
- Parity’yi etkileyen her değişiklikte plan fazı referansı belirtilmelidir.
