# Tile Loading Pipeline Refaktör Planı

**Tarih**: 2026-02-03  
**Hedef**: Google Earth benzeri hızlı ve kararlı tile loading sistemi

---

## Mevcut Sorunlar

| Sorun | Etki | Öncelik |
|-------|------|---------|
| **1. Karmaşık kod yapısı** | SyncRasterTiles 800+ satır, anlaşılması zor | Kritik |
| **2. Worker thread sayısı** | ~~1 thread~~ → 8 thread (düzeltildi) | ✅ |
| **3. Scheduler karmaşıklığı** | İki sistem (scheduler + legacy) çakışıyor | Kritik |
| **4. Tile state yönetimi** | Birden fazla state enum, tutarsız | Yüksek |
| **5. Texture upload** | Frame başına limit çok düşük | Orta |
| **6. Mesh build** | Ayrı bir adım, yavaşlatıyor | Orta |

---

## Google Earth Mimari Karşılaştırma

| Bileşen | Google Earth | sardaglobe (Mevcut) | Hedef |
|---------|-------------|---------------------|-------|
| Tile Pipeline | `UNLOADED→SCHEDULED→FETCHING→DECODING→UPLOADING→READY` | Karmaşık dual-system | Tek basit pipeline |
| Worker Threads | Thread pool (8-16) | ~~1~~ 8 (düzeltildi) | ✅ |
| LOD Selection | SSE-based | Sa table | SSE ile hibrit |
| Priority Queue | Distance + Viewport + Level | Karmaşık scoring | Basitleştirilmiş |
| Texture Upload | Batched, frame-sync | Sınırlı per-frame | Artırılmış limit |

---

## Refaktör Planı

### Faz 1: Temizlik (1-2 gün)
**Hedef**: Mevcut kodu basitleştir

1. **Scheduler'ı tamamen kaldır**
   - Legacy download sistemi yeterli
   - İki sistem yerine tek sistem
   
2. **SyncRasterTiles'ı bölme**
   - `SyncRasterTiles_CreateTiles()` - Tile oluşturma
   - `SyncRasterTiles_QueueDownloads()` - Download queue
   - `SyncRasterTiles_ProcessReady()` - Texture upload
   - `SyncRasterTiles_BuildMeshes()` - Mesh oluşturma
   - `SyncRasterTiles_Eviction()` - Cache eviction

3. **Tek bir Tile State kullan**
   ```cpp
   enum class TileState {
       UNLOADED,   // Hiç yüklenmemiş
       QUEUED,     // Download kuyruğunda
       LOADING,    // HTTP request devam ediyor
       DECODING,   // Decode ediliyor
       READY,      // Texture + Mesh hazır
       FAILED      // Yükleme başarısız
   };
   ```

### Faz 2: Download Pipeline (1 gün)
**Hedef**: Hızlı ve basit download

1. **Basit priority hesaplama**
   ```cpp
   float priority = 0.0f;
   priority += isLeaf ? 100.0f : 0.0f;           // Leaf bonus
   priority += (maxZoom - tile.z) * 10.0f;       // Zoom proximity
   priority += 1.0f / (1.0f + distanceToCamera); // Distance (normalized)
   ```

2. **Download worker pool**
   - 8-16 concurrent worker (mevcut: 8 ✅)
   - Round-robin veya priority-based job distribution

3. **Stale job cancellation**
   - 2 saniyeden eski job'ları atla
   - Viewport dışına çıkan tile'ları cancel et

### Faz 3: Texture/Mesh Pipeline (1 gün)
**Hedef**: Frame drop'suz upload

1. **Texture upload limit artır**
   - Frame başına 32-64 texture (mevcut: ~16)
   - GPU upload batching

2. **Mesh build parallelization**
   - Mesh'leri worker thread'de build et
   - Sadece GPU upload main thread'de

3. **Pre-built mesh cache**
   - Flat tile mesh'leri önceden oluştur
   - DEM mesh'leri lazy build

### Faz 4: LOD & Visibility (2 gün)
**Hedef**: Doğru tile seçimi

1. **SSE-based LOD (Google Earth style)**
   ```cpp
   float ComputeSSE(const Tile& tile, const Camera& camera) {
       float geometricError = EARTH_CIRCUMFERENCE / (pow(2.0, tile.z) * 256);
       float distance = glm::length(tile.center - camera.position);
       float sse = (geometricError / distance) * (viewportHeight / (2.0 * tan(fov/2)));
       return sse;
   }
   
   if (sse > SSE_THRESHOLD) subdivide();
   else render();
   ```

2. **Frustum culling optimizasyonu**
   - Tile bounding sphere check
   - Early rejection for off-screen tiles

3. **Fallback logic basitleştirme**
   - Parent tile hazırsa children'ı bekle
   - Değilse parent'ı göster

---

## Dosya Yapısı Önerisi

```
src/
├── tile/
│   ├── tile.h              # Tile struct, TileState enum
│   ├── tile_key.h          # TileKey with QuadKey support
│   ├── tile_loader.h       # Download & decode pipeline
│   ├── tile_loader.cpp
│   ├── tile_renderer.h     # Texture upload & mesh build
│   ├── tile_renderer.cpp
│   ├── tile_selector.h     # LOD selection, visibility
│   └── tile_selector.cpp
├── globe_engine.h          # Main engine (simplified)
└── globe_engine.cpp        # Reduced from 11000+ to ~5000 lines
```

---

## Öncelik Sırası

| Sıra | Görev | Süre | Etki | Durum |
|------|-------|------|------|-------|
| 1 | Scheduler devre dışı | 30 dk | Kararlılık | ✅ Tamamlandı |
| 2 | 8 worker thread | 15 dk | Hız | ✅ Tamamlandı |
| 3 | Worker thread decode | 1 saat | Hız | ✅ Tamamlandı |
| 4 | Texture upload limit (64) | 15 dk | Hız | ✅ Tamamlandı |
| 5 | Download queue size (512) | 5 dk | Hız | ✅ Tamamlandı |
| 6 | SSE-based LOD | - | Doğruluk | ✅ Zaten mevcut |

---

## Hemen Yapılabilecek Değişiklikler

### 1. Texture Upload Limit Artırma
```cpp
// globe_engine.cpp
constexpr size_t kMaxReadyDownloadsPerFrame = 32;  // 16 -> 32
```

### 2. Mesh Rebuild Limit Artırma
```cpp
// globe_engine.cpp
maxRebuilds = 64;  // 32 -> 64
```

### 3. Download Queue Size Artırma
```cpp
// globe_engine.cpp
constexpr size_t kMaxDownloadQueueSize = 512;  // 256 -> 512
```

---

## Başarı Kriterleri

- [ ] Tile loading süresi: < 500ms (ilk görünüm)
- [ ] Frame drop: < 5% (zoom sırasında)
- [ ] Memory kullanımı: < 512MB (normal kullanımda)
- [ ] Tile görünürlük: %100 (görünür tile'ların hepsi yüklü)
- [ ] Code complexity: SyncRasterTiles < 200 satır

---

## Notlar

- Her faz sonunda test ve commit
- Breaking change yapmadan önce backup
- Performans metrikleri her adımda ölçülmeli
