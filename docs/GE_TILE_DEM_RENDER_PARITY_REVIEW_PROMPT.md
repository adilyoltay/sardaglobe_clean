# GE Tile/DEM Render Parity — Lead Engineer Review Prompt

> **Amaç:** Bu prompt, Native Globe engine'in tile fetch → decode → upload → mesh build → DEM sampling → stitch → skirt → render frame pipeline'ını Google Earth WASM/Pro referansıyla karşılaştırarak P0 ve P1 blocker'ları tespit etmek için hazırlanmıştır. Tek parça kopyalanabilir formattadır.

---

## PROMPT (Kopyala & Yapıştır)

```
Sen bir Lead Graphics/Engine Engineer'sın. Aşağıda Native Globe adlı bir globe rendering engine'in
tile + DEM + render frame pipeline'ının detaylı teknik özeti var. Bu özeti Google Earth (WASM + Pro Desktop)
referansıyla karşılaştır ve şu soruları yanıtla:

1. Bu pipeline Google Earth kalitesinde bir globe deneyimi vaat edebilir mi?
2. P0 (ship-blocker) ve P1 (quality-blocker) eksiklikleri nelerdir?
3. Her blocker için 1-2 cümlelik çözüm önerisi ver.

═══════════════════════════════════════════════════════════════════════════════
A. TILE FETCH & DECODE PIPELINE (Raster Imagery)
═══════════════════════════════════════════════════════════════════════════════

Mevcut Akış:
  1. LOD Selection (LodSelector::Select)
     - SSE-based quadtree traversal (Screen-Space Error)
     - Frustum culling + Horizon culling (HorizonCuller, geometrik test)
     - SSE hysteresis (0.80 ratio) — LOD flip-flop önlenir
     - Neighbor LOD conformance (maxNeighborDelta=1, 6 pass)
     - Child quorum: 4/4 children render-ready olana kadar parent tutulur
     - minLodPixels=256 (sub-pixel tile culling, GE parity)
     - Adaptive SSE: altitude + tilt bazlı threshold ayarı
     - Quality mode multiplier (LOW/MEDIUM/HIGH/ULTRA → 4.0/2.0/1.4/1.0)

  2. Tile Request & Priority
     - TileStateMachine: Unloaded → Scheduled → Fetching → Decoding → Uploading → Ready
     - 3-tier priority: Low (prefetch), Normal (required), Urgent (leaf + quorum children)
     - Weighted scheduler: SSE + center-bias + aging + directional-predictive scoring
     - Score-ranked request loop (RankedTile list)
     - Stale state recovery: timeout per state (Scheduled=3s, Fetching=20s, Decoding=12s)
     - Exponential backoff on failure (1s → 32s, unlimited retry for placeholder tiles)
     - Cancel after N frames untouched (cancelAfterFramesUntouched=120)

  3. Fetch & Decode
     - TileScheduler: async HTTP fetch (CURL) + decode (stb_image)
     - Memory cache (1GB) + Decoded cache (1GB) + Disk cache (tile_cache/)
     - 3-layer cache hierarchy: disk → memory → decoded → GPU
     - Priority bump on re-request (GE-style rank upgrade)

  4. Texture Upload
     - TextureManager: time-budgeted GPU upload (uploadBudgetMs)
     - PBO async upload (ring buffer, 8 PBO × 4MB, GL_ARB_sync fence)
     - Fallback chain: Texture2DArray → Atlas → PBO → Immediate
     - Eviction callback → DEM co-eviction (raster evict = DEM evict)
     - mostlyBlackOpaqueRaster detection → ancestor underlay

  GE Referans Karşılaştırması:
     ✅ SSE-based LOD (GE §3 parity)
     ✅ Child quorum (GE §4 parity)
     ✅ Priority-ranked fetch (GE §5 parity)
     ✅ PBO async upload (GE §7 parity)
     ✅ Horizon culling (GE §6 parity)
     ⚠️ 3-stage frame pipeline (DoFrame→BuildNextScene→RenderScene) kısmen var
         (SceneSnapshot struct mevcut ama tam ayrılmamış)
     ❌ Texture2DArray varsayılan olarak kapalı (useTexture2DArray=false)
     ❌ RASTER_CROSSFADE shader-level (mevcut ama atlas/array modunda test edilmemiş)

═══════════════════════════════════════════════════════════════════════════════
B. DEM (ELEVATION) FETCH & SAMPLING PIPELINE
═══════════════════════════════════════════════════════════════════════════════

Mevcut Akış:
  1. DEM Provider
     - Dual provider: GoogleEarth (Elevation API, max zoom 22) | TerrainRGB (Terrarium/Mapbox, max 15)
     - Auto-fallback: GE auth fail → terrain-rgb otomatik geçiş
     - Health check on startup (AuthFailed, Blocked, Unreachable, BadResponse)
     - Worker thread pool + priority queue (priority 0-2, score-ranked)
     - Auth backoff (3 consecutive fails → 30s backoff)
     - Terminal error state (prevents log spam)
     - GE epoch auto-detect from RockMesh/PlanetoidMetadata

  2. DEM Cache & Pinning
     - LRU cache (512 entries, O(1) eviction)
     - Visible DEM pinning (leaves + 1-ring neighbors, budget=1024)
     - Edge-coherent DEM key pinning (ancestor keys)
     - Co-eviction: raster evict → DEM evict (prevents orphan DEM entries)

  3. DEM Grid Data
     - 17×17 grid per tile (289 samples, GE parity — eski 5×5'ten upgrade)
     - Bilinear interpolation (SampleBilinear)
     - No-data sanitization (< -11000m → 0m replacement)
     - Height scale: base_scale × exaggeration (1.0 × 2.5 = 2.5x)
     - Terrain variance pre-computation (adaptive LOD input)

  4. DEM Coherence (Seam/Cliff Prevention)
     - Per-edge DEM level computation (common ancestor of adjacent DEM keys)
     - demEdgeLevelPack: 4×uint8 packed (N,E,S,W edge levels)
     - Edge blend band (demEdgeBlendSegments=2 vertex rings)
     - Cascade coarsening guard (demMaxCoarseningDeltaLod=2)
     - demPending reason tracking (OWN_TARGET, EDGE_COHERENT, NEIGHBOR_PARENT)
     - Stable DEM frame counter (stableDemFrames) for latch reset
     - Latch-based seam-skirt mask (prevents gap→skirt oscillation)

  GE Referans Karşılaştırması:
     ✅ 17×17 DEM grid (GE §8 parity — en.kh Elevation API format)
     ✅ Edge-coherent DEM sampling (GE §9 parity)
     ✅ DEM pinning against eviction (GE §10 parity)
     ✅ No-data sanitization (GE parity)
     ⚠️ DEM batch fetch yok (maxBatchSize=1, eski parity 10 idi)
     ⚠️ Height exaggeration 2.5x sabit (GE'de kullanıcı ayarlı, genelde 1.0-1.5x)
     ❌ GPU heightmap path kaldırıldı (sadece CPU mesh bake)

═══════════════════════════════════════════════════════════════════════════════
C. MESH BUILD & STITCH & SKIRT PIPELINE
═══════════════════════════════════════════════════════════════════════════════

Mevcut Akış:
  1. Mesh Build (TileMeshBuilder::Build)
     - Vertex format: pos(3) + normal(3) + uv(2) + heightKm(1) = 9 floats/vertex
     - Adaptive mesh segments: halve per 2 zoom levels, floor at max(demMeshN-1, 8)
     - Web Mercator interpolation (Mercator Y space, not linear latitude)
     - WGS84 ellipsoid (km units)
     - RTE/RTC origin split (double→float hi/lo for jitter-free GPU rendering)
     - DEM interior sampler + 4 edge-coherent samplers (N,E,S,W)
     - Edge blend: influence-weighted interpolation (interior↔edge sampler)
     - Corner sampler selection: strongest local influence, coarsest level tie-break
     - Missing height fill: 4-pass neighborhood propagation + global mean fallback
     - Normal computation: finite difference with outside-tile sampling for border normals
     - Height sanitization: clamp [-12km, +12km], NaN → 0

  2. Stitch (Edge Conformance)
     - edgeCoarserMask: per-edge delta-LOD detection (4 cardinal directions)
     - stitchMask: delta=1 → stitch (vertex snapping to coarser grid)
     - delta>1 → skirt fallback (stitch only handles single-level difference)
     - edgeFinerMask: finer-neighbor detection (no redundant skirts)
     - Stitch-aware mesh template variants (MeshTemplate with stitchMask)
     - Shared EBO (index buffer reuse across same-segment tiles)

  3. Skirt Generation (GenerateSkirts)
     - Selective skirts (skirtMask per edge, not all-4-edges)
     - Height-aware skirt depth (depth scales with tile height range)
     - Config: skirtDepthNearKm=0.03, skirtDepthFarKm=0.15, skirtDepthRatio=0.003
     - Same-LOD DEM mismatch skirt (flat vs displaced neighbor → skirt)
     - Latched seam-skirt mask (prevents oscillation)

  4. Mesh Revision & Scheduling
     - meshRevision / meshBuiltRevision guard (single-commit per build cycle)
     - RevisionReasons: TOPOLOGY, DEM_PENDING, DEM_TARGET, SEGMENT_MISMATCH, EDGE_PACK
     - TileMeshScheduler: worker thread pool + time-budgeted GPU upload
     - Stale DEM upgrade acceptance (accept even if revision mismatch for flat→DEM)
     - One-shot revision guard (prevent every-frame mesh rebuild churn)

  GE Referans Karşılaştırması:
     ✅ Adaptive mesh segments (GE §11 parity)
     ✅ RTE/RTC origin split (GE §12 parity — double precision emulation)
     ✅ Edge-coherent DEM sampling at border vertices (GE §9 parity)
     ✅ Selective skirts (GE §13 parity — seam hiding)
     ✅ Stitch mask for delta=1 LOD boundaries (GE §14 parity)
     ✅ One-shot mesh revision (GE §15 parity — rebuild churn prevention)
     ⚠️ uCornerLods bilinear LOD interpolation: computed but shader usage limited
     ⚠️ Terrain morph (flat→displaced) distance-based mode disabled for CPU_MESH_BAKE
     ❌ GPU terrain morph (uTerrainMorph uniform) — only CPU-side, no shader blend

═══════════════════════════════════════════════════════════════════════════════
D. RENDER FRAME PIPELINE
═══════════════════════════════════════════════════════════════════════════════

Mevcut Akış:
  1. Update Phase (GlobeEngine::Update)
     - Flight controller update (momentum, animations)
     - Dynamic near/far planes (altitude-based, near=max(1m, alt×1%))
     - LOD selection → leaf set
     - Temporal leaf hold (0.5s hold for gap-free pan/zoom)
     - Render-time child quorum (collapse non-renderable leaves to ancestor)
       - Policy: NoTile/NoMesh → immediate collapse; NoTerrain → full sibling; NoTexture → 2+ blocked
     - Edge mask computation (edgeCoarserMask, skirtMask, stitchMask)
     - DEM target level computation (effective level from neighbor coherence)
     - demEdgeLevelPack computation (common ancestor DEM for border vertices)
     - Mesh revision bump (single-commit, reason-tracked)
     - DEM pinning + DEM update
     - Scheduler update (fetch/decode result processing)
     - Texture upload (time-budgeted)
     - Mesh build queue + result processing

  2. Render Phase (RenderFrame::DrawTiles)
     - 3-pass rendering (GE-style gap-free):
       Pass 0: Placeholder tiles (loading texture, underneath everything)
       Pass 1: Fallback ancestor tiles (opaque, depth-biased behind leaves)
       Pass 2: Renderable leaves (crossfade or solid)
     - Unpop/Crossfade (GE-style smooth appearance):
       - Per-tile fade animation (300ms default, speed-adaptive)
       - Shader-level raster crossfade (uUnpopBlend + parent texture UV remap)
       - Speed gate: >120 km/s → shorten fade; >900 km/s → bypass
       - ComputeUnpopUvTransform: leaf→ancestor UV mapping with Mercator correction
     - Terrain morph: time-based with staggered start (hash-based per-tile spread)
     - Instanced flat tile batching (draw-call reduction for non-DEM tiles)
     - Front-to-back sorting (camera distance) for early-Z efficiency
     - Polygon offset for fallback tiles (2.0, 4.0 — prevents z-fighting)
     - Transparent pixel detection → ancestor underlay

  3. Depth Precision
     - Log-depth (logDepthEnabled=true, default)
     - Reversed-Z alternative (glDepthFunc(GL_GEQUAL), infinite far plane)
     - Mutual exclusion: logDepth OR reversedZ, never both
     - RTE/RTC: tile origin hi/lo split → jitter-free vertex positions

  4. Debug & Telemetry
     - ImGui debug panel with comprehensive stats
     - Per-frame timing: LOD select, request loop, scheduler, texture upload, DEM update, edge mask
     - Gap-free telemetry: renderableLeaves, crossfading, fallback, placeholder, missing
     - Render-time quorum telemetry: downgrades, noMesh, noTexture, noTerrain
     - DEM convergence telemetry: pendingReasons, coarseningCascade, edgePackRebuilds
     - Frame time tracker (avg, P95, P99)

  GE Referans Karşılaştırması:
     ✅ 3-pass gap-free rendering (GE §16 parity)
     ✅ Unpop/crossfade with speed gate (GE §17 parity)
     ✅ RTE/RTC jitter-free rendering (GE §12 parity)
     ✅ Log-depth / Reversed-Z precision (GE §18 parity)
     ✅ Front-to-back sorting (GE §19 parity)
     ✅ Render-time child quorum (GE §20 parity — prevents mixed-LOD tearing)
     ⚠️ 3-stage frame pipeline (DoFrame→BuildNextScene→RenderScene) not fully separated
     ⚠️ Atmosphere/sky dome yok (visual completeness)
     ⚠️ Water rendering yok (GE'de 4-stage water scene graph)
     ❌ Label rendering yok (GE'de DrawableNearCameraQueue + text atlas)
     ❌ Vector tile overlay yok (vectorEnabled=false)

═══════════════════════════════════════════════════════════════════════════════
E. ROCKMESH / NODEDATA (3D Buildings) PIPELINE
═══════════════════════════════════════════════════════════════════════════════

Mevcut Akış:
  - RockMeshManager: fetch NodeData from GE endpoint (quadkey-based)
  - LOD-aware mesh management (seed quadkeys + camera-driven loading)
  - Child-LOD proximity selection (geMeshChildLodDistance=5000m)
  - HTTP/2 transport with connection reuse
  - Octree discovery (BulkMetadata + PlanetoidMetadata)
  - Vertex explosion mitigation (AABB discard, distance sanity, non-finite guard)
  - RTE/RTC rendering (origin split for jitter-free building rendering)
  - Fallback texture for textureless meshes

  GE Referans Karşılaştırması:
     ✅ NodeData format parsing (GE §21 parity)
     ✅ Octree traversal (GE §22 parity)
     ✅ RTE/RTC rendering (shared with tile pipeline)
     ⚠️ Texture atlas for building textures (not implemented)
     ⚠️ LOD transition smoothing (pop visible between child/parent LOD)

═══════════════════════════════════════════════════════════════════════════════
F. CAMERA & NAVIGATION
═══════════════════════════════════════════════════════════════════════════════

  - PerspectiveCamera: double-precision lat/lon/alt + ECEF
  - FlightController: orbit, pan, zoom, tilt, momentum
  - Globe picking (ray-sphere intersection for terrain interaction)
  - FlyTo animation (spline/linear interpolation)
  - Terrain-aware camera (altitude clamped to terrain surface)

  GE Referans Karşılaştırması:
     ✅ Double-precision camera (GE parity)
     ✅ Orbit/pan/zoom/tilt (GE navigation RE parity)
     ✅ FlyTo animation (GE StagedAutopilotModel simplified parity)
     ⚠️ Bounce interpolator yok
     ⚠️ PredictiveViewPrefetcher yok (momentum-based prefetch)

═══════════════════════════════════════════════════════════════════════════════
G. ÖZELLİK MATRİSİ (Parity Scorecard)
═══════════════════════════════════════════════════════════════════════════════

  | Kategori                    | Parity | Notlar                                    |
  |-----------------------------|--------|-------------------------------------------|
  | SSE LOD Selection           | 95%    | Adaptive, hysteresis, quality modes        |
  | Tile State Machine          | 95%    | Full lifecycle, stale recovery             |
  | Fetch Priority/Scheduling   | 90%    | Weighted, aging, directional               |
  | Texture Upload              | 85%    | PBO ready, Array disabled by default       |
  | DEM Fetch & Cache           | 85%    | 17×17, pinning, co-eviction                |
  | DEM Edge Coherence          | 90%    | Per-edge levels, blend band, latch         |
  | Mesh Build                  | 90%    | Adaptive segments, RTE, edge samplers      |
  | Stitch & Skirt              | 85%    | Delta=1 stitch, selective skirts           |
  | Render Frame                | 85%    | 3-pass, crossfade, front-to-back           |
  | Depth Precision             | 90%    | Log-depth + Reversed-Z + RTE              |
  | Camera/Navigation           | 90%    | Full orbit/pan/zoom, FlyTo                 |
  | 3D Buildings (RockMesh)     | 70%    | Basic NodeData, no texture atlas           |
  | Atmosphere/Sky              | 0%     | Yok                                        |
  | Water Rendering             | 0%     | Yok                                        |
  | Labels/Annotations          | 0%     | Yok                                        |
  | Vector Tiles                | 0%     | Disabled                                   |

  TOPLAM PIPELINE PARITY: ~75-80% (core rendering pipeline ~88%)

═══════════════════════════════════════════════════════════════════════════════
H. SORULAR
═══════════════════════════════════════════════════════════════════════════════

Yukarıdaki pipeline özetine dayanarak:

1. P0 BLOCKER'LAR (Ship-Blocker — Bu olmadan ürün gösterilemez):
   Her P0 için: [Sorun] → [Etki] → [Çözüm önerisi, ~tahmini efor]

2. P1 BLOCKER'LAR (Quality-Blocker — Bu olmadan GE parity iddia edilemez):
   Her P1 için: [Sorun] → [Etki] → [Çözüm önerisi, ~tahmini efor]

3. Pipeline'da gizli bir teknik borç veya mimari risk var mı?
   (Ör: single-thread bottleneck, cache coherence race, precision cliff)

4. Bu engine "Google Earth kalitesinde globe" vaadini karşılayabilir mi?
   Kısa, dürüst değerlendirme.
```

═══════════════════════════════════════════════════════════════════════════════

## ÖN-DOLDURULMUŞ CEVAP ŞABLONU (Referans)

Aşağıdaki bölüm, codebase analizi sonucu tespit edilen P0/P1 blocker'ların ön listesidir:

### P0 BLOCKER'LAR (Ship-Blocker)

| # | Sorun | Etki | Çözüm | Efor |
|---|-------|------|-------|------|
| P0-1 | **Atmosphere/Sky dome yok** | Uzaydan bakışta siyah arka plan; GE'de mavi halo + gradient sky | Rayleigh/Mie scattering shader veya basit sky dome + gradient | 3-5 gün |
| P0-2 | **Texture2DArray varsayılan kapalı** | `useTexture2DArray=false` → atlas bleeding hâlâ üretimde; tile kenarlarında renk sızması | Flag'ı true yap, atlas path'i fallback olarak tut; regression testi | 1-2 gün |
| P0-3 | **3-stage frame pipeline tam ayrılmamış** | Update+Render aynı thread'de monolitik; BuildNextScene snapshot var ama kullanılmıyor → frame-time spike'lar, input latency | SceneSnapshot'ı aktif kullan: BuildNextScene() → snapshot → RenderScene(snapshot) | 3-5 gün |
| P0-4 | **DEM height exaggeration 2.5x sabit** | Dağlar gerçekçi değil; Everest 22km gibi görünüyor; GE'de 1.0-1.5x | `demExaggerationFactor`'ı 1.0 yap (true elevation), UI slider ekle | 0.5 gün |
| P0-5 | **Distance-based terrain morph kapalı** | `effectiveUseDistanceBasedMorph = false` hardcoded; DEM pop-in görünür | CPU_MESH_BAKE modunda bile enable et, phase-mismatch'i adjacent tile sync ile çöz | 2-3 gün |

### P1 BLOCKER'LAR (Quality-Blocker)

| # | Sorun | Etki | Çözüm | Efor |
|---|-------|------|-------|------|
| P1-1 | **Water rendering yok** | Okyanuslar düz raster texture; GE'de reflective water + depth-based color | Su mask + basit water shader (normal map, specular) | 5-8 gün |
| P1-2 | **Label/annotation rendering yok** | Şehir/ülke isimleri gösterilemiyor; GE'de text atlas + DrawableNearCameraQueue | SDF text atlas + billboard rendering pipeline | 8-12 gün |
| P1-3 | **uCornerLods shader'da aktif kullanılmıyor** | Bilinear LOD interpolation hesaplanıyor ama vertex shader'da tam uygulanmıyor → LOD sınırlarında sert geçiş | Vertex shader'da cornerLods uniform'unu position blending'e bağla | 2-3 gün |
| P1-4 | **DEM batch fetch devre dışı** | `maxBatchSize=1` → her tile ayrı HTTP request; GE'de 10 tile/batch | Batch endpoint'i (CN=N) tekrar aktifle, response parser'ı restore et | 2-3 gün |
| P1-5 | **GPU terrain morph (shader-level) yok** | Terrain flat→displaced geçişi CPU-only; tile başına mesh rebuild gerektirir → micro-stutter | `uTerrainMorph` uniform'u shader'a ekle; displacement = mix(flat, displaced, morph) | 3-5 gün |
| P1-6 | **PredictiveViewPrefetcher yok** | Kamera momentum yönünde pre-fetch yapılmıyor; pan sırasında tile loading gecikir | Camera velocity vector'den directional prefetch zone hesapla | 2-3 gün |
| P1-7 | **RockMesh texture atlas yok** | 3D bina texture'ları tek tek bind; draw-call patlaması yüksek LOD'da | RockMesh-specific texture atlas veya bindless texture path | 5-8 gün |
| P1-8 | **RockMesh LOD transition pop** | Parent↔child LOD geçişinde ani pop; GE'de crossfade | RockMesh fade-in (alpha blend parent → child, 200-300ms) | 2-3 gün |

### TEKNİK BORÇ / MİMARİ RİSKLER

| # | Risk | Etki | Öneri |
|---|------|------|-------|
| R1 | **Update() 2000+ satır monolitik fonksiyon** | Edge mask + DEM coherence + mesh revision hepsi iç içe; debug/maintenance zorluğu | Alt-fonksiyonlara ayır: `ComputeEdgeMasks()`, `ResolveDemCoherence()`, `CommitMeshRevisions()` |
| R2 | **Render-time quorum O(32 pass × leaf²)** | Worst case'de 32 iterasyon; yüksek tile count'ta (1400+) frame spike | Topological sort ile single-pass collapse; veya max 4-6 pass hard limit |
| R3 | **DEM coherence cascade** | Neighbor instability zincir reaksiyonu: tile A → coarsen → tile B → coarsen → ... | Coherence dampening: max 1 level change per frame per tile |
| R4 | **CPU mesh bake single authority** | GPU heightmap path kaldırıldı; her DEM değişikliğinde full mesh rebuild | Uzun vadede hybrid path: GPU displacement map + CPU fallback |
| R5 | **Test coverage ~30%** | 66 test dosyası var ama integration/visual testler yetersiz | Automated screenshot regression + per-component unit test suite |

### SONUÇ DEĞERLENDİRMESİ

**Bu engine Google Earth kalitesinde globe vaadini karşılayabilir mi?**

Core rendering pipeline (tile fetch → DEM → mesh → render) **~88% GE parity** seviyesinde.
Teknik altyapı (SSE LOD, child quorum, RTE/RTC, edge coherence, 3-pass rendering, unpop crossfade)
büyük ölçüde GE WASM RE bulgularına uygun implemente edilmiş.

**Ancak "Google Earth deneyimi" için eksik olan katmanlar:**
- Atmosphere (P0) — görsel bütünlük için zorunlu
- Texture2DArray production-ready (P0) — bleeding artefact'ı ortadan kaldırır
- Water + Labels (P1) — "harita" deneyimi için minimum gerekli
- Terrain morph shader (P1) — pop-free DEM geçişleri

**Realistik değerlendirme:** P0'lar kapatılırsa (~2 hafta efor), engine **demo-ready** seviyeye ulaşır.
P1'ler kapatılırsa (~6-8 hafta ek efor), **"GE-like experience"** iddia edilebilir seviyeye ulaşır.
Full GE parity (labels, KML, vector tiles, 3D buildings LOD, water) için **3-6 ay** ek geliştirme gerekir.
