# 3D Terrain Parity Geliştirme Planı

**Tarih:** 2026-02-05  
**Hedef:** Google Earth benzeri 3D terrain deneyimi (yakın zoom + tilt + orbit)  
**Kapsam:** DEM/elevation pipeline, shader displacement, terrain-aware kamera, tile mesh ve LOD/gecis kalitesi  
**Tahmini Sure:** 2-3 hafta

---

## Ana Kural ve Oncelik

Bu plan `AGENTS.md` kurallarina baglidir:

1. **API/Behavior parity onceliklidir** (`globe-web-html/libs/webglobe.js` referansi).
2. **Navigasyon davranisinda Google Earth parity onceliklidir** (tilt/orbit/pan/zoom).
3. Mimari iyilestirmeler parity'yi bozmayacak sekilde ilerler.

---

## Mevcut Durum (Derin Inceleme Sonucu)

### Kritik blocker'lar

1. **DEM kaynagi runtime'da guvenilir degil (401/403 riski):** elevation akisi kiriliyor.
2. **Terrain-aware picking yok:** orbit/pan pivot sphere tabanli oldugu icin GE hissi olusmuyor.
3. **Displacement authority belirsiz (CPU + GPU birlikte):** cift displacement ve seam riski.
4. **DEM request onceliklendirmesi zayif:** yakin/ekrandaki tile gec gelebiliyor.
5. **Progressive fallback/morph eksik:** child DEM gec geldiginde ani "pop" goruluyor.
6. **Terrain/nav parity testleri eksik:** regressions erken yakalanamiyor.

### Sonuc

Su anki uygulama, parca parca 3D terrain ozelliklerine sahip olsa da "Google Earth benzeri yakin 3D arazi deneyimi" hedefi icin kritik zincir tamam degil.

---

## Hedef Mimari (Bu Plan Sonu)

1. DEM akis sagligi startup'ta dogrulanmis ve izlenebilir olacak.
2. Tek displacement authority modeli uygulanacak (CPU veya GPU, karisik degil).
3. Kamera pivot/pick terrain-aware calisacak.
4. DEM scheduler gorunurluk/SSE odakli olacak.
5. Parent->child progressive terrain gecisi morph ile yumusatilacak.
6. Otomatik testler ve kabul metrikleriyle parity regressions engellenecek.

---

## Faz Plani

## FAZ 0 - Altyapi Sagligi ve Telemetri
**Sure:** 1-2 gun  
**Oncelik:** P0

### Gorevler

1. DEM endpoint health-check: startup'ta auth/erişim testi, hata kodu siniflandirma.
2. DEM download metriği: success/fail, HTTP code, retry/backoff nedeni.
3. Failover stratejisi: DEM yoksa acik log + kontrollu fallback (flat terrain mode indicator).
4. Konfigurasyon netligi: DEM URL, header/token, timeout, retry/backoff parametreleri tek yerden yonetilsin.

### Ciktilar

- Runtime'da DEM'in neden gelmedigi net gorulen telemetri.
- "Sessiz flat mode" yerine bilincli fallback durumu.

### DoD

- DEM endpoint erisim hatalari log/metrics'te gorunur.
- DEM yokken uygulama stabil, durum acikca raporlanir.

---

## FAZ 1 - Elevation Pipeline Stabilizasyonu (Tek Authority)
**Sure:** 3-4 gun  
**Oncelik:** P0

### Gorevler

1. Displacement authority secimi:
   - `CPU_MESH_BAKE` veya `GPU_HEIGHTMAP_DISPLACE` runtime flag.
   - Ayni anda iki model aktif olmayacak.
2. Shader contract tamamlama:
   - `uHeightScale`, `uHeightMin`, `uHeightMax` gercekten etkili olacak.
   - Uniform set edilmediginde deterministic fallback.
3. Edge/seam stratejisi:
   - Secilen authority'e gore komsu LOD gecisleri dogrulanacak.
4. Tile mesh + DEM baglantisi:
   - Tile'ın elevation alip almadigi debug overlay ile gorunur olacak.

### Ciktilar

- Cift displacement kaldirilmis tutarli terrain yuksekligi.
- Shader/mesh davranisi tek kaynakla dogrulanmis.

### DoD

- Terrain yuksekligi her tile icin tek kaynaktan uretiliyor.
- LOD kenarinda belirgin seam/catlak kabul esigini asmiyor.

---

## FAZ 2 - Terrain-Aware Kamera ve Interaksiyon
**Sure:** 3 gun  
**Oncelik:** P0

### Gorevler

1. `TerrainPicker` implementasyonu:
   - Ray -> visible terrain tile intersection.
   - Parent fallback (child DEM yoksa parent ile tahmini pick).
2. FlightController entegrasyonu:
   - Orbit pivot terrain uzerinde.
   - Zoom-to-cursor terrain lock.
   - Pan anchor terrain-aware.
3. GE parity tuning:
   - Tilt limiti, orbit merkezi, scroll/shift-scroll davranisi.

### Ciktilar

- Tilt/orbit/pan davranisi araziye kilitli hissedecek.

### DoD

- Cursor altindaki terrain ile orbit pivot uyumlu.
- Duz sphere pivot davranisi sadece explicit fallback modunda.

---

## FAZ 3 - DEM Scheduler ve Progressive LOD Gecisi
**Sure:** 3-4 gun  
**Oncelik:** P1

### Gorevler

1. DEM request queue onceliklendirme:
   - SSE/ekran alanı/mesafe bazli priority queue.
   - `unordered` iterasyon yerine deterministic oncelik.
2. Iptal ve stale request yonetimi:
   - Kameradan cikan tile isteklerini dusur.
3. Progressive terrain:
   - Child DEM gelene kadar parent DEM remap.
   - Arrival aninda morph (100-250 ms) ile yumusak gecis.
4. Pop azaltma:
   - Frame-budgetli upload ve goruntu kararliligi.

### Ciktilar

- Yakinda gorunen terrain once gelir.
- Ani yukseklik "pop" etkisi belirgin sekilde azalir.

### DoD

- Hedef kamera senaryolarinda goze batan pop/catlak yok.
- DEM gecikse bile gecici parent fallback ile tutarli yuzey korunur.

---

## FAZ 4 - Test, Parity Benchmark ve Release Gate
**Sure:** 2-3 gun  
**Oncelik:** P1

### Gorevler

1. Otomatik testler:
   - DEM fetch/auth/fallback testleri.
   - Terrain-aware picking unit+integration testleri.
   - Displacement authority regression testleri.
2. Goruntu benchmark seti:
   - Daglik, vadili, kıyı ve sehir senaryolari.
   - Sabit kamera snapshot karsilastirmasi.
3. Navigasyon parity checklist:
   - Orbit merkez davranisi.
   - Tilt siniri ve geri donus.
   - Zoom-to-cursor arazi takibi.
4. CTest/CI gate:
   - Terrain testleri fail ise merge engeli.

### Ciktilar

- Tekrarlanabilir parity dogrulama paketi.

### DoD

- Tum terrain/nav testleri green.
- Kritik parity checklist maddeleri kabul edildi.

---

## Is Paketi / Dosya Eslestirme

| Paket | Ana Dosyalar |
|------|---------------|
| DEM saglik + telemetri | `src/io/dem_manager.cpp`, `src/core/config.h` |
| Tek displacement authority | `src/rendering/tile_mesh_builder.cpp`, `src/rendering/shader_manager.h`, `src/rendering/shader_manager.cpp`, `src/rendering/tile_renderer.cpp`, `src/rendering/render_frame.cpp` |
| Terrain picker + kamera | `src/engine/globe_engine.cpp`, `src/camera/flight_controller.cpp`, yeni `src/terrain/terrain_picker.*` (gerekirse) |
| Priority scheduler + progressive | `src/io/dem_manager.*`, `src/scheduling/lod_selector.*`, `src/core/tile.h` |
| Test/benchmark | `tests/*`, gerekli ise `tools/*` |

---

## Takvim (Oneri)

1. **Hafta 1:** FAZ 0 + FAZ 1
2. **Hafta 2:** FAZ 2 + FAZ 3
3. **Hafta 3:** FAZ 4 + polish

---

## KPI ve Kabul Esikleri

1. DEM fetch success rate (gorunur tile seti): **>= %95** (ag normali altında).
2. Terrain-aware pick basari orani (test senaryolari): **>= %99**.
3. Yakindan tilt/orbit senaryosunda buyuk pop/catlak: **0 kritik olay**.
4. Kamera hareketinde FPS dususu (terrain aktifken): kabul edilen hedefe uygun (proje profil hedefleriyle uyumlu).

---

## Riskler ve Azaltim

| Risk | Etki | Azaltim |
|------|------|---------|
| DEM servisi auth/downtime | Yuksek | Startup health-check, fallback indicator, cache stratejisi |
| Shader/mesh authority gecis regressions | Yuksek | Flag bazli rollout + regression test |
| Terrain pick performans maliyeti | Orta | Visible-tile siniri, erken-cikis, cache |
| LOD gecis artifact'lari | Yuksek | Morph + edge policy + benchmark snapshot |

---

## Faz Durum Takibi

| Faz | Durum | Not |
|-----|-------|-----|
| FAZ 0 | ✅ Done | DEM health + telemetri (DemStats, CheckHealth, debug panel) |
| FAZ 1 | ✅ Done | Tek displacement authority (DisplacementMode enum, CPU/GPU gate) |
| FAZ 2 | ✅ Done | Terrain-aware picking (iterative DEM refinement in PickGlobe) |
| FAZ 3 | ✅ Done | Priority DEM (ranked request ordering via SSE score) |
| FAZ 4 | Not Started | Test + parity gate |

---

## Yurutme Notu

Her faz tamamlandiginda su dokuman da guncellenecek:

- `docs/API_PORT_REVIEW_PROMPT.md`
  - Faz Tamamlama Gunlugu
  - Guncel Durum Snapshot
  - Gerekirse Mevcut Implementasyonlar

Bu gereklilik `AGENTS.md` ile uyumludur.
