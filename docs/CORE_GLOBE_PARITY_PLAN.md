# SardaGlobe — Core Globe Parity Plan (Kod-Doğrulamalı Tek Doküman)

> **Tarih:** 2026-02-12  
> **Parity Hedefi:** Sadece Google Earth  
> **Ana Referans:** `docs/GOOGLE_EARTH_TILE_DEM_RENDER_DEEP_ANALYSIS.md`  
> **Kapsam:** Temel globe fonksiyonları (tile yaşam döngüsü, LOD, mesh, DEM, render pipeline, terrain-aware navigation)

---

## 1) Doğrulama Yöntemi

Bu doküman, kod tabanındaki mevcut implementasyon doğrudan incelenerek güncellendi.

- İncelenen ana modüller:
  - `src/engine/*`
  - `src/scheduling/*`
  - `src/rendering/*`
  - `src/io/*`
  - `src/camera/*`
  - `src/core/*`
  - `tests/*`
- Çalıştırılan test seti:
  - `ctest --test-dir build --output-on-failure` → **35/35 green**
  - Kritik regresyonlar: `TileStateMachineCancelTest`, `TileSchedulerCancelFlowTest`, `TileFetcherCancelRerequestTest`, `DemCoEvictionTest`, `UnpopCrossfadePolicyTest`, `CornerLodTest`, `DepthPrecisionTest`

---

## 2) Önceki Dokümandaki Tutarsızlıklar (Düzeltilenler)

1. **“P0-P6 tamamı done” ifadesi hâlâ kısmen doğru.**
   - Çekirdek mimaride önemli ilerleme var; ancak core parity gate’i için görsel regresyon ve cache hiyerarşisi kalemleri eksik.

2. **“SSE LOD neighbor conformance tam değil” bulgusu kapanmış durumda.**
   - `P0.1` sonrası `LodConformanceTest` geçiyor.

3. **“Tile State Machine tam parity değil” bulgusu kapanmış durumda.**
   - `P0.2` ile `Canceled` state + `Event::Cancel` akışı eklendi; `FetchStart/DecodeStart/UploadStart/Evict` ile birlikte yaşam döngüsü tamamlandı.

4. **“4-layer cache mevcut” ifadesi hâlâ doğru değil.**
   - Pratikte disk/network + GPU texture yönetimi var; GE’deki net GPU→Memory→Disk→Network hiyerarşisi ve layer hit/miss zinciri parity seviyesinde değil.

5. **“Vector katman implementasyonu mevcut” ifadesi operasyonel olarak doğru değil.**
   - `LayerManager` ve veri yapıları var ancak render pipeline’a entegre değil.

Bu doküman, yukarıdaki düzeltmeleri esas alır.

---

## 3) Kod-Doğrulanmış Parity Durumu

Durum etiketleri:
- `✅`: Kodda var ve aktif kullanılıyor
- `🟡`: Kısmi / parity için eksik bağlantılar var
- `❌`: Kodda yok

### 3.1 Temel Tile Yaşam Döngüsü

| Alan | Durum | Not |
|---|---|---|
| Tile kimliği (quadkey/parent-child/neighbor/wrap) | ✅ | `TileKey` güçlü ve testli |
| SSE tabanlı LOD traversal | ✅ | Frustum+horizon + ranked required/prefetch |
| Neighbor conformance | ✅ | `LodConformanceTest` geçiyor |
| Tile state machine tanımı | ✅ | 8 state / 12 event (`Canceled` + `Cancel`) |
| Tile state machine yaşam döngüsüne tam entegrasyon | ✅ | `FetchStart/DecodeStart/UploadStart/Cancel/Evict` akışta aktif |
| Gap-free render fallback (parent/placeholder/leaf) | ✅ | 3-pass yaklaşım aktif |
| GE unpop + gerçek parent-child crossfade | ✅ | Shader-level parent/child raster crossfade aktif |

### 3.2 DEM / Mesh / 3D Terrain

| Alan | Durum | Not |
|---|---|---|
| DEM health check + telemetri | ✅ | Health, auth/backoff, metrikler var |
| DEM request priority + dedupe | ✅ | Priority+score+seq ile queue |
| DEM batch RPC parity (`BatchGetElevationsByPoint`) | ❌ | Özel HTTP endpoint + custom parse |
| CPU mesh bake | ✅ | DEM sampling + normal + skirts |
| GPU heightmap displacement modu | 🟡 | Var, fakat sağlık koşullu ve parity geçişleri eksik |
| Terrain-aware picking (parent fallback ile) | ✅ | Iterative sphere refinement + DEM sample |
| Terrain morph (flat→DEM veya parent→child) | ✅ | GPU heightmap + CPU mesh bake path için geçişli morph var |
| `uCornerLods` bilinear LOD interpolation | 🟡 | Kodda var; görsel regresyon gate’i eksik |
| Depth plane denklemleri | ❌ | Kodda yok |
| Skirt üretimi | ✅ | Mesh builder + shared EBO ile aktif |

### 3.3 Render ve Frame Mimarisi

| Alan | Durum | Not |
|---|---|---|
| Update + Render ayrımı | ✅ | Oyun döngüsünde ayrık fonksiyonlar var |
| GE 3-stage frame (`DoFrame → BuildNextScene → RenderScene`) | ✅ | Scene snapshot ile BuildNextScene -> RenderScene ayrımı aktif |
| Request-driven frame (`RequestNewFrame`) | ✅ | Dirty/event tabanlı frame request ve idle sleep aktif |
| Reversed-Z / Log depth | 🟡 | Log-depth ve reversed-Z aktif; görsel z-fighting gate’i eksik |
| Atmosphere / sky pass | ❌ | Atmosfer render modülü yok |

### 3.4 Performans ve Cache

| Alan | Durum | Not |
|---|---|---|
| URL template parser (regex-free) | ✅ | Segment tabanlı parser aktif |
| Fetch worker + CURL thread-local reuse | ✅ | Pooling + cancel hook aktif |
| Decode priority + fairness | ✅ | Urgent batch/fairness var |
| Upload priority/budget + texture reuse | ✅ | Budget ve `glTexSubImage2D` kullanılıyor |
| Async mesh scheduler + revision kontrolü | ✅ | Worker + stale result discard |
| Epoch pin + budgeted eviction | ✅ | Uygulama aktif |
| Touch-based cancel lifecycle | ✅ | `cancelAfterFramesUntouched` + viewport-out cancel aktif |
| DEM/raster co-eviction | ✅ | `demRasterCoEviction` + `DemManager::UnpinAndEvict` aktif |
| Drop metriği (queue overflow) | ✅ | Queue overflow sayaçları aktif artıyor |
| Decoded memory cache layer telemetrisi | ✅ | Pre-decoded RGBA LRU + decode bypass sayaçları aktif |
| Memory cache layer telemetrisi | ✅ | Compressed tile LRU + hit/miss/write/evict sayaçları aktif |
| Disk cache layer telemetrisi | ✅ | Hit/miss/write sayaçları + debug panel görünümü |
| Net 4-layer cache parity | 🟡 | GPU/DecodedMemory/Memory/Disk/Network katmanları var; invalidasyon/promotion tuning kısmi. GE tarafında `EarthMemoryManagerImpl` + touch-based loader cancel + `cancel_old_fetches` sinyalleri var. |
| Texture atlas / instancing batch | ✅ | Atlas + instanced flat-tile batch path aktif, atlas compaction/eviction politikası bağlı |
| JobSystem üretimde aktif | ❌ | Sınıf var, pratikte kullanılmıyor. GE WASM string kanıtı: `JobDispatcher` + `WorkerPoolJobRunner` + `AddClosure/AddJob` (next-frame/delay) + `"Alarm"` tabanlı delayed scheduling. |
| Predictive view prefetch (P5.1) | ✅ | Kamera momentum'a göre 1-2s ileri projeksiyon + ranked prefetch aktif |
| Near-camera render sort (P5.2) | ✅ | Fallback + leaf pass için front-to-back sıralama aktif |
| Terrain+imagery fetch koordinasyonu (P5.3) | 🟡 | Koordineli request + DEM-aware mesh delay + parent DEM fallback aktif; dedicated koordinasyon testi açık |

### 3.5 Navigasyon

| Alan | Durum | Not |
|---|---|---|
| Pan / Orbit / Zoom / Tilt / momentum | ✅ | Kullanımda |
| Double-click fly-to | ✅ | Kullanımda |
| Terrain-aware orbit/pan anchor | 🟡 | Temel mevcut; GE parity ince tuning eksik |
| On-demand render ile nav jank kontrolü | ✅ | Request-driven frame + input/event request akışı aktif |

### 3.6 Test ve Gate Durumu

| Alan | Durum | Not |
|---|---|---|
| CTest otomatik test sayısı | ✅ | 35 test mevcut |
| LOD conformance | ✅ | `LodConformanceTest` pass |
| Predictive prefetch regression | ✅ | `PredictivePrefetchTest` pass |
| Core sistem testi | ✅ | `GlobeSystemTest` pass |
| Edge mask testi | ✅ | `EdgeMaskTest` pass |
| Render parity (unpop/corner/depth) otomatik test | 🟡 | `TileFadeTest` + `CornerLodTest` var; depth görsel gate eksik |
| Terrain parity acceptance gate | 🟡 | `TileTerrainMorphTest` + `TileCacheStatsTest` var; görsel gate eksik |

---

## 4) Tek Tutarlı Parity Geliştirme Planı (Yüksek → Düşük)

Bu plan yalnızca temel globe parity boşluklarına odaklanır.

## P0 — Doğruluk ve Yaşam Döngüsü Stabilizasyonu (Kritik)

### P0.1 LOD neighbor conformance ihlalini kapat
- Durum: ✅ Tamamlandı (2026-02-06)
- Hedef: `LodConformanceTest` green.
- İş:
  - `EnforceNeighborConformance` algoritmasını düzelt.
  - Kamera konumuna bağlı ihlal senaryolarını regression teste sabitle.
- Çıkış kriteri:
  - `ctest` içinde `LodConformanceTest` pass.

### P0.2 Tile state machine’i gerçek yaşam döngüsüne tam bağla
- Durum: ✅ Tamamlandı (2026-02-12)
- Hedef: Event akışı deterministik olsun.
- İş:
  - `FetchStart/DecodeStart/UploadStart/Evict` olaylarını gerçek akışta kullan.
  - `Canceled` state + `Event::Cancel` geçişlerini scheduler cancel akışına bağla.
  - Cancel edilen fetch/decode sonuçlarını fail/retry akışından ayrıştır (`FetchResult.canceled`).
- Çıkış kriteri:
  - Ölü event kalmaması.
  - State geçiş logunda illegal geçiş olmaması.

### P0.3 Drop/backpressure telemetrisi gerçek sayaç üretsin
- Durum: ✅ Tamamlandı (2026-02-06)
- Hedef: Queue baskısı ölçülebilsin.
- İş:
  - Drop sayaçlarının gerçekten increment edildiği akışı bağla.
  - Debug panelde anlamlı değer üretsin.
- Çıkış kriteri:
  - Stres testte sayaçlar deterministic artış göstermeli.

---

## P1 — Görsel Temel Parity (Core Globe UX)

### P1.1 Unpop + parent-child crossfade
- Durum: ✅ Tamamlandı (2026-02-06, engine scope)
- Hedef: pop-free tile geçişleri.
- İş:
  - Parent/child aynı frame blend.
  - Kamera hızına bağlı bypass/shorten kuralı.
  - Shader’da çift texture + blend uniform.
- Çıkış kriteri:
  - Hızlı zoom/pan sırasında görünür pop = 0 kritik olay.
- Uygulanan:
  - `RenderFrame` içinde unpop geçişi sırasında parent/child aynı frame blend akışı kuruldu.
  - Kamera hızına göre unpop süresi dinamik kısaltılıyor; yüksek hızda unpop bypass ediliyor.
  - Shader-level raster crossfade eklendi: `uPhotoTileTextureUnpop` + `uUnpopBlend` + `uTexScaleOffsetUnpop` + `uRasterCrossfade`.
  - Child tile UV’sinden ancestor texture UV’sine dönüşüm (scale/offset) bağlandı.
  - Debug panelde `Crossfading` ve `Cam Speed` telemetrileri eklendi.
- Test:
  - `ctest --test-dir build --output-on-failure` (35/35 geçti).
  - Yeni `TileFadeTest`: değişken fade süresinde monoton alpha ve clamp davranışı doğrulandı.

### P1.2 `uCornerLods` bilinear LOD interpolation
- Durum: 🟡 Kısmi tamamlandı (2026-02-06)
- Hedef: mixed-LOD sınırlarında geometri sürekliliği.
- İş:
  - Tile köşe LOD değerleri hesaplama.
  - Vertex shader’a `uCornerLods` akışı.
- Çıkış kriteri:
  - LOD sınırında seam/jitter gözlenmemesi.
- Uygulanan:
  - `edgeCoarserMask` -> `uCornerLods` (NW/NE/SE/SW) dönüşümü eklendi.
  - Tile başına `uCornerLods` uniform akışı render tarafında bağlandı.
  - Vertex shader’da `uCornerLods` ile bilinear LOD interpolation ve `textureLod` sampling etkinleştirildi.
  - Heightmap texture’larında mip chain üretimi aktif edildi (`glGenerateMipmap`).
- Test:
  - Yeni `CornerLodTest`: edge mask -> corner lod mapping doğrulandı.
  - `ctest --test-dir build --output-on-failure` (35/35 geçti).
- Kalan parity farkı:
  - Görsel seam/jitter doğrulaması henüz otomatik görüntü tabanlı regresyon testi ile güvenceye alınmadı.

### P1.3 Terrain morph transition
- Durum: ✅ Tamamlandı (2026-02-06)
- Hedef: DEM geldiğinde ani sıçrama olmasın.
- İş:
  - Mesh morph factor (150-250ms).
  - Flat→DEM ve parent→child terrain geçişini yumuşat.
- Çıkış kriteri:
  - Yakın tilt/orbit sırasında terrain pop olmaması.
- Uygulanan:
  - Tile yaşam döngüsüne 200ms `terrainMorph` state’i eklendi (`flat -> displaced`).
  - Shader’da `uTerrainMorph` ile displacement ve normal perturbation kademeli hale getirildi.
  - Heightmap render pass’ında tile-bazlı morph uniform akışı bağlandı.
  - CPU mesh-bake path için vertex başına `heightKm` attribute eklendi ve shader’da geometri morph aktif edildi.
- Test:
  - Yeni `TileTerrainMorphTest`: başlangıç, orta, tamamlanma ve reset/restart davranışı doğrulandı.
  - `ctest --test-dir build --output-on-failure` (35/35 geçti).

### P1.4 Depth precision iyileştirmesi
- Durum: 🟡 Kısmi tamamlandı (2026-02-06)
- Hedef: z-fighting kritik düzeyde sıfırlansın.
- İş:
  - Reversed-Z veya log-depth yaklaşımı.
  - Kamera near/far’ı terrain-aware ayar.
- Çıkış kriteri:
  - Z-fighting kritik olay = 0.
- Uygulanan:
  - Log-depth precision path eklendi (`uUseLogDepth`, `uLogDepthFar`, `gl_FragDepth`).
  - Reversed-Z alternatifi eklendi (kamera projeksiyonu + `GL_GEQUAL` depth state).
  - Depth far değeri frame bazında kameradan shader’a geçiriliyor.
  - Debug panelde `Log Depth Precision` ve `Reversed-Z Precision` toggle’ları eklendi.
- Test:
  - Yeni `DepthPrecisionTest`: standart ve reversed-Z near/far NDC mapping + ray tutarlılığı doğrulandı.
  - `ctest --test-dir build --output-on-failure` (35/35 geçti).
- Kalan parity farkı:
  - Z-fighting için otomatik görsel regresyon/eşik testi henüz yok.

---

## P2 — GE Frame Mimari Parity

### P2.1 3-stage frame pipeline
- Durum: ✅ Tamamlandı (2026-02-06)
- Hedef: `DoFrame → BuildNextScene → RenderScene` benzeri ayrım.
- İş:
  - Build çıktısını snapshot olarak üret.
  - Render aşamasını scene snapshot’tan besle.
- Çıkış kriteri:
  - Pipeline süreleri ayrı ölçülür ve stabil.
- Uygulanan:
  - `Update` sonunda scene snapshot üretiliyor (`mvp`, `leafSet`, render state).
  - `Render` yalnızca snapshot tüketiyor; render input’u update sırasında sabitleniyor.
- Test:
  - `ctest --test-dir build --output-on-failure` (35/35 geçti).

### P2.2 Request-driven frame (`RequestNewFrame`)
- Durum: ✅ Tamamlandı (2026-02-06)
- Hedef: sürekli loop yerine olay tetiklemeli render davranışı.
- İş:
  - Dirty/event tabanlı frame request mekanizması.
- Çıkış kriteri:
  - Idle durumda gereksiz frame üretimi azalır.
- Uygulanan:
  - `requestDrivenFrame` konfigürasyonu eklendi.
  - Idle durumda render/swap atlanıp kısa sleep uygulanıyor.
  - Input callback’leri, kamera hareketi, queue aktivitesi ve fade/morph animasyonları frame request üretiyor.
- Test:
  - `ctest --test-dir build --output-on-failure` (35/35 geçti).

---

## P3 — Core Performans Parity

### P3.1 Cache hiyerarşisini 4 katman parity’ye yaklaştır
- Durum: 🟡 Kısmi tamamlandı (2026-02-06)
- Hedef: GPU→Memory→Disk→Network davranışını net katmanlaştır.
- İş:
  - Katman bazlı hit/miss telemetrisi.
  - Eviction ve refill sıralarını katmanlı hale getir.
- Çıkış kriteri:
  - Revisit latency anlamlı düşüş.
- Uygulanan:
  - LRU tabanlı thread-safe `Decoded Memory Cache` katmanı eklendi (pre-decoded RGBA blob).
  - LRU tabanlı thread-safe `MemoryTileCache` katmanı eklendi.
  - Fetch akışı decoded-memory -> memory -> disk -> network şeklinde bağlandı; disk hit’ler memory’ye promote ediliyor.
  - Decoder tarafında decoded blob fastpath eklendi; cache hit’te image codec bypass ediliyor.
  - Disk cache için hit/miss/write/fail/byte sayaçları eklendi.
  - Scheduler stats ve debug panelde decoded/memory/disk cache + decode bypass + network fetch metrikleri yayınlanıyor.
  - `TileCacheStatsTest` + `MemoryTileCacheTest` + `DecodedTileBlobTest` + `TileDecoderBlobFastpathTest` ile sayaç/LRU/fastpath davranışı otomatik testlendi.
- Kalan parity farkı:
  - GPU eviction sonrası layer-level invalidasyon/promotion politikası ve adaptive byte budget tuning GE seviyesinde değil.

### P3.2 Atlas/instancing tabanlı draw-call azaltımı
- Durum: ✅ Tamamlandı (2026-02-06)
- Hedef: render maliyetini düşür.
- İş:
  - Tile atlas veya eşdeğer batching.
- Çıkış kriteri:
  - Görünür tile arttığında draw-call eğrisi yumuşar.
- Uygulanan:
  - Debug panelde `Draw Calls` ve `Triangles` telemetrileri eklendi (atlas/instancing öncesi baseline).
  - `TextureAtlasAllocator` eklendi (sayfa/slot allocation + free + UV transform üretimi).
  - `TextureManager` upload yoluna atlas path bağlandı: tile RGBA atlas slotuna `glTexSubImage2D` ile yükleniyor, tile başına `texScaleOffset` atanıyor.
  - Shader tarafına ana raster için `uTexScaleOffsetMain` uniform’u eklendi; tile render path’i atlas UV remap kullanır hale getirildi.
  - Shader-level unpop/crossfade UV hesabı atlas-aware compose edildi (`ancestor texScaleOffset` + parent-child relative UV).
  - Atlas-aware eviction sıralaması eklendi (yüksek atlas page’leri önce boşaltma) ve trailing boş page trim aktif.
  - Atlas defrag/compaction eklendi: üst sayfalardaki slotlar boşluk olan alt sayfalara GPU tarafında taşınıp page sayısı sıkıştırılıyor.
  - Flat/no-terrain/no-crossfade atlas tile’ları için instanced batch render path eklendi (`DrawElementsInstanced`), draw call telemetrisi `Instanced batches/tiles` ile izleniyor.
  - Debug panelde atlas telemetrisi eklendi (`Atlas Slots`, `capacity`, `page`).
  - `TextureAtlasAllocatorTest` allocation/reuse/UV + trailing-page trim davranışını doğrulayacak şekilde genişletildi.
  - Yeni `AtlasGutterUvTest` ile gutter + UV inset davranışı doğrulandı.
- Kalan parity farkı:
  - P3.2 kapsamında kalan kritik açık yok.

---

## P4 — Core Dışı İkinci Dalga

- Atmosphere/sky
- Vector tile render entegrasyonu
- Label/KML/overlay
- Terrain occlusion culling (ileri)

Bu kalemler core parity tamamlandıktan sonra ele alınmalı.

---

## P5 — GE Pro Desktop RE Bulgularından Parity İyileştirmeleri

> **Kaynak:** `docs/GOOGLE_EARTH_PRO_DESKTOP_RE_ANALYSIS.md` (GE Pro 7.3.6 native binary RE)
> **Odak:** `earth::evll` namespace'inden çıkarılan mimari kanıtlar

### P5.1 PredictiveViewPrefetcher — Öngörücü Tile Prefetch
- Durum: ✅ Tamamlandı (2026-02-06)
- GE Kanıtı: `earth::evll::PredictiveViewPrefetcher` — kamera momentum vektörüne bakarak henüz görünür olmayan tile'ları önceden fetch eder.
- Hedef: Pan/zoom sırasında beyaz tile ("pop") sayısını minimize etmek.
- Mevcut durum:
  - Statik neighbor/child prefetch korunur.
  - Buna ek olarak kamera hızına dayalı öngörücü prefetch ve öngörücü sıralama aktif.
- Uygulanan:
  1. `LodSelector::Select()` imzasına `cameraVelocity` eklendi.
  2. `LodSelector::AddPredictivePrefetch()` eklendi:
     - hız eşiği: `>= 0.05 km/s`
     - ileri projeksiyon: `1.0-2.0s`
     - adaylar: leaf komşuları + uygun child'lar
     - filtre: yönsel hizalanma / predicted distance iyileşmesi + frustum guard
  3. `TilePyramid::Select()` ve `BuildRankedLists()` velocity ile beslendi.
  4. Prefetch ranking, predictive aktifken `1 / (predicted_distance + 1)` + yönsel boost ile hesaplanır.
  5. `GlobeEngine::Update()` içinde frame-delta'dan `cameraVelocityKmPerSec_` türetilip `TilePyramid`'e geçirildi.
- Dokunulacak dosyalar:
  - `src/scheduling/lod_selector.h` — `Select()` imzasına `cameraVelocity` parametresi ekle
  - `src/scheduling/lod_selector.cpp` — Predicted prefetch mantığı
  - `src/scheduling/tile_pyramid.h/.cpp` — Velocity'yi `Select`'e akıt
  - `src/engine/globe_engine.cpp` — `FlightController`'dan velocity çıkar, `TilePyramid`'e geçir
- Çıkış kriteri:
  - Sabit hızda pan sırasında `Pending > 0` frame oranı %30'dan %10'un altına düşer.
  - Mevcut prefetch davranışı bozulmaz (statik neighbor/child prefetch korunur).
- Test:
  - Mevcut tüm CTest seti regression geçti (`35/35`).
  - Yeni `PredictivePrefetchTest` eklendi:
    - tiny velocity (`<0.05 km/s`) için prefetch seti değişmiyor (threshold gate),
    - normal velocity için en az bir senaryoda prefetch seti genişliyor.
- Tahmini süre: 1-2 gün

### P5.2 Near-Camera Render Sort — Kamera-Mesafe Bazlı Render Sırası
- Durum: ✅ Tamamlandı (2026-02-06)
- GE Kanıtı: `earth::evll::DrawableNearCameraQueue` — kameraya en yakın drawable'lar önce render edilir (front-to-back). `DrawableFIFOQueue` ile birlikte iki farklı priority queue.
- Hedef: GPU early-Z rejection ile overdraw azaltmak ve z-fighting'i iyileştirmek.
- Mevcut durum:
  - Önceki sürümde fallback level-bazlı, leaf pass alpha-bazlıydı.
  - Kamera mesafesi render sırasına dahil değildi.
- Uygulanan:
  1. `SceneSnapshot` yapısına `cameraPos` eklendi ve `Update` sırasında yazılıp `Render` tarafına taşındı.
  2. `RenderFrame::DrawTiles()` imzasına `cameraPos` parametresi eklendi.
  3. Fallback tile sıralaması level yerine front-to-back (`distance(tile.center, cameraPos)`) olacak şekilde güncellendi.
  4. Leaf sıralaması:
     - önce `alpha >= 1.0` (opaque),
     - sonra `alpha < 1.0` (crossfade),
     - her iki grup içinde front-to-back.
- Dokunulacak dosyalar:
  - `src/rendering/render_frame.h` — `DrawTiles()` imzasına `cameraPos` parametresi ekle
  - `src/rendering/render_frame.cpp` — Front-to-back sıralama mantığı
  - `src/engine/globe_engine.h` — `SceneSnapshot`'a `cameraPos` ekle
  - `src/engine/globe_engine.cpp` — `cameraPos`'u snapshot'a yaz ve `DrawTiles`'a geçir
- Çıkış kriteri:
  - Overdraw oranı ölçülebilir düşüş gösterir (debug panel'de triangle/pixel ratio).
  - Z-fighting olayları azalır (özellikle uzak tile'larda).
  - Mevcut gap-free 3-pass render davranışı bozulmaz.
- Test:
  - Mevcut tüm CTest seti regression geçti (`35/35`).
  - Görsel doğrulama adımı (overdraw/z-fighting etkisi) manuel acceptance gate olarak açık.
- Tahmini süre: 0.5 gün

### P5.3 Terrain+Imagery Fetch Koordinasyonu
- Durum: 🟡 Kısmi ilerletildi (2026-02-06)
- GE Kanıtı: `earth::evll::FetchQnImageHandler` + `earth::evll::FetchQnTerrainHandler` — terrain ve imagery ayrı handler'larla fetch ediliyor ama aynı QuadNode traversal'dan koordineli tetikleniyor.
- Hedef: DEM ve imagery fetch'ini koordine ederek terrain pop süresini azaltmak.
- Mevcut durum:
  - `TileScheduler` imagery fetch'i yönetiyor (`tile_scheduler.cpp`).
  - `DemManager` terrain DEM fetch'i yönetiyor (`dem_manager.cpp`).
  - Bu adımda request koordinasyonu ve DEM-aware mesh delay eklendi.
- Uygulanan:
  1. **Koordineli request:** `Update()` içinde imagery request döngüsü sırasında DEM request aynı key/priority/score ile tetikleniyor.
  2. **DEM pending görünürlüğü:** `DemManager::HasPendingRequest(key)` (queued + in-flight) eklendi.
  3. **DEM-aware mesh build delay:** `QueueMeshBuild` içinde:
     - `CPU_MESH_BAKE` + DEM healthy + DEM pending/in-flight ise mesh build en fazla 500ms erteleniyor.
     - Timeout sonrası flat mesh build'e düşülüyor; DEM geldiğinde mevcut revision akışı rebuild tetikliyor.
  4. **Parent DEM fallback:** `DemManager::SampleHeight` exact tile yoksa ancestor zincirinden (parent -> ... -> root) bilinear örnekleme yapıyor.
     - Exact child DEM geldiğinde öncelik tekrar child tile'a dönüyor.
     - `DemManager::HasDataOrAncestor(key)` ile mesh scheduler, parent fallback varken gereksiz 500ms beklemeyi bypass ediyor.
  5. **Telemetri:** `DebugStats` içine `demWaitMs`, `meshRebuildCount`, `leafUnderflowFrames`, `seamEdgeCount`, `avgEdgeHeightDeltaM`, `tilesUsingAncestorDem` eklendi, debug panelde yayınlanıyor.
- Kalan:
  1. **Dedicated test:** `FetchCoordinationTest` benzeri imagery+DEM koordinasyon testinin eklenmesi.
- Dokunulacak dosyalar:
  - `src/engine/globe_engine.cpp` — Request döngüsünü birleştir, DEM-aware mesh build koşulu
  - `src/engine/globe_engine.h` — `DebugStats`'a yeni metrikler
  - `src/io/dem_manager.h/.cpp` — `HasPendingRequest(key)` metodu ekle
  - `src/rendering/tile_mesh_scheduler.h/.cpp` — DEM timeout ile ertelenmiş build desteği
- Çıkış kriteri:
  - Yakın zoom'da terrain pop süresi ölçülebilir azalır (DEM→mesh latency düşer).
  - Gereksiz mesh rebuild sayısı %50+ azalır.
  - DEM olmayan durumda (endpoint down) mevcut davranış korunur.
- Test:
  - Mevcut tüm CTest seti regression geçti (`35/35`).
  - Yeni `DemFallbackTest`: parent fallback örnekleme + exact-child önceliği doğrulandı.
  - Yeni `DemContinuityTest`: exact child + ancestor fallback komşuluğunda edge delta eşiği doğrulandı.
  - Dedicated `FetchCoordinationTest` henüz eklenmedi.
- Tahmini süre: 1-2 gün

---

## 5) Kabul Kriterleri (Core Parity Gate)

Core parity “tamamlandı” demek için aşağıdaki gate’ler zorunludur:

1. `LodConformanceTest` dahil tüm CTest seti green.
2. Hızlı pan/zoom senaryosunda:
   - Kritik pop: 0
   - Kritik seam: 0
   - Kritik z-fighting: 0
3. Tile lifecycle:
   - Stuck state: 0
   - Illegal transition: 0
4. Terrain-aware etkileşim:
   - Orbit/pan/zoom target drift kabul eşiği içinde.
5. Frame metrikleri:
   - p95/p99 regressions gate’i geçer.

---

## 6) Kısa Sonuç

- Engine altyapısı güçlü ve üretime yakın bir çekirdek oluşturuyor.
- Core parity'de kritik eksikler artık `z-fighting için görsel gate`, `4-layer cache için promotion/refill stratejisinin tamamlanması`, `görsel regresyon gate'leri`.
- **P5 (GE Pro Desktop RE):** `P5.1` ve `P5.2` tamamlandı. Kalan tek açık: terrain+imagery fetch koordinasyonu (`P5.3`).
- Bu doküman artık parity için tek kaynak olarak kullanılmalı; "done" etiketi yalnızca yukarıdaki gate'ler geçildiğinde verilmelidir.
