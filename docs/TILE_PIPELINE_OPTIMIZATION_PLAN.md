# Tile Pipeline Optimizasyon Planı

**Versiyon:** 1.2  
**Durum:** FINAL ✅  
**Tarih:** 2026-02-05  
**Hedef:** Main-thread hitch azaltma, öncelik inversiyonu düzeltme, queue overflow retry döngüsü kaldırma

---

## Kritik Varsayımlar & Kısıtlamalar

| Parametre | Varsayılan Değer | Açıklama |
|-----------|------------------|----------|
| `maxConcurrentFetches` | **16** | HTTP worker sayısı |
| `maxConcurrentDecodes` | **8** | Decode worker sayısı |
| `maxInFlightFetches` | **64** | Toplam pending+active fetch limiti |
| `MAX_TEXTURE_UPLOADS_PER_FRAME` | **8** | Frame başına max texture upload |
| `TEXTURE_UPLOAD_BUDGET_MS` | **2.0** | Texture upload time budget (ms) |
| `meshSchedulerWorkers` | **4** | Mesh build worker sayısı |
| `MAX_MESH_REBUILDS_PER_FRAME` | **4** | Frame başına max mesh GPU upload |
| `meshUploadBudgetMs` | **2.0** | Mesh GPU upload time budget (ms) |
| Deferred mipmap (P4.4) | **OFF** | Görsel kalite öncelikli |
| PBO async upload | **OFF** | GL 3.3 uyumluluk |

**Thread-Safety Varsayımları:**
- `DemManager::GetHeightSampler()` **thread-safe** kabul edilir (read-only cache access)
- Shared EBO ownership `MeshTemplate` sınıfına aittir, tile eviction'da silinmez

---

## Pipeline Özeti

```
LOD Select → Request → Fetch → Decode → Upload → Mesh Build → Render
    ↓           ↓         ↓        ↓         ↓          ↓          ↓
TilePyramid  Scheduler  Fetcher  Decoder  TexManager  MeshBuilder  RenderFrame
(main)       (main)     (16 wrk) (8 wrk)  (main+bgt)  (4 wrk)      (main)
```

---

## Faz Özeti

| Faz | Ad | Öncelik | Tahmini Süre | Etki |
|-----|----|---------|--------------|------|
| P0 | Telemetri & Görünürlük | Önkoşul | 2 saat | Ölçülebilirlik |
| P1 | Fetch/Cache Hattı | Yüksek | 3 saat | %50-70 fetch hızı |
| P2 | Fetcher/Decoder Priority | Yüksek | 2 saat | Urgent latency |
| P3 | Backpressure & Queue | Orta | 2 saat | Retry loop çözümü |
| P4 | Texture Upload | Yüksek | 3 saat | GPU hitch azalır |
| P5 | Async Mesh Pipeline | Kritik | 4 saat | Frame stutter çözümü |
| P6 | Pin/Eviction & Micro-Opt | Düşük | 2 saat | Alloc spike azalır |

**Toplam Tahmini Süre:** ~18 saat

---

## P0 — Telemetri & Görünürlük (Önkoşul)

### Hedef
Bottleneck'leri ölçülebilir hale getirmek; p95/p99 frame-time ve pipeline sürelerini görmek.

### Uygulama

#### P0.1 Frame Timing Ring Buffer
```cpp
// src/core/frame_time_tracker.h (header-only)
#pragma once
#include <array>
#include <algorithm>

namespace globe {

struct FrameTimings {
    double lodSelectMs = 0;
    double requestLoopMs = 0;
    double schedulerUpdateMs = 0;
    double textureUploadMs = 0;
    double meshBuildMs = 0;
    double renderMs = 0;
    double totalMs = 0;
};

class FrameTimeTracker {
    std::array<double, 300> frameTimes_;
    int writeIndex_ = 0;
public:
    void Record(double ms);
    double GetP95() const;
    double GetP99() const;
    double GetAvg() const;
};
```

#### P0.2 Pipeline Counters
```cpp
// tile_scheduler.h - ekleme
struct SchedulerStats {
    std::atomic<int> queueWaitCount{0};
    std::atomic<double> avgFetchDurationMs{0};
    std::atomic<double> avgDecodeDurationMs{0};
    int droppedFetch = 0;
    int droppedDecode = 0;
};
```

**Telemetri Lokasyonu (Tek Dosya):**
- `FrameTimeTracker` → `src/core/frame_time_tracker.h` (header-only)
- `SchedulerStats` → `src/scheduling/tile_scheduler.h`
- `DebugStats` → `src/engine/globe_engine.h` (mevcut)

#### P0.3 Debug Panel Güncelleme
`RenderDebugPanel()` içine:
- p95/p99 frame-time
- Alt süre breakdown (LOD, fetch, decode, upload, mesh, render)
- Queue sizes (fetch pending, decode pending, upload pending)
- Active fetch count

### Dosya Değişiklikleri
| Dosya | Değişiklik |
|-------|------------|
| `src/core/frame_time_tracker.h` | `FrameTimings`, `FrameTimeTracker` (header-only) |
| `src/engine/globe_engine.cpp` | `Update()` ve `Render()` timing |
| `src/scheduling/tile_scheduler.h` | `SchedulerStats` struct |
| `src/io/tile_fetcher.cpp` | Fetch duration tracking |
| `src/io/tile_decoder.cpp` | Decode duration tracking |

### Kabul Kriterleri
- [ ] Debug panelde p95/p99 frame-time görünür
- [ ] "Pending fetch/decode" ve "active fetch" değerleri izlenebilir
- [ ] Alt süre breakdown çalışıyor

---

## P1 — Fetch/Cache Hattı (Main-thread I/O & Regex Giderimi)

### Hedef
Request aşamasında disk I/O ve regex maliyetini kaldırmak.

### P1.1 URL Template Parser

#### Analiz
Mevcut kod (`tile_scheduler.cpp:31-37`):
```cpp
std::string TileScheduler::BuildUrl(const TileKey& key) const {
    std::string url = config_.tileUrl;
    url = std::regex_replace(url, std::regex("\\{z\\}"), std::to_string(key.level));
    url = std::regex_replace(url, std::regex("\\{x\\}"), std::to_string(key.x));
    url = std::regex_replace(url, std::regex("\\{y\\}"), std::to_string(key.y));
    return url;
}
```

#### Yeni Tasarım
```cpp
// src/io/tile_url_template.h
class TileUrlTemplate {
public:
    explicit TileUrlTemplate(const std::string& templateUrl);
    std::string Build(int z, int x, int y) const;
    
private:
    struct Segment {
        enum Type { Literal, PlaceholderZ, PlaceholderX, PlaceholderY };
        Type type;
        std::string text;  // Literal için
    };
    std::vector<Segment> segments_;
};
```

**Beklenen Kazanç:** %90+ URL build hızı (regex → sprintf)

### P1.2 Disk Cache I/O Worker'a Taşı

#### Mevcut Sorun
`TileScheduler::Request()` main thread'de cache read yapıyor:
```cpp
if (cache_->Read(key, config_.tileUrl, cachedData)) {
    // ... decode'a gönder
}
```

#### Yeni Tasarım
```cpp
// download_types.h - FetchRequest güncelleme
struct FetchRequest {
    TileKey key;
    std::string url;
    Priority priority = Priority::Normal;
    float score = 0.0f;
    
    // Cache callbacks (optional)
    std::function<bool(const TileKey&, std::vector<uint8_t>&)> tryReadCache;
    std::function<void(const TileKey&, const std::vector<uint8_t>&)> writeCache;
};
```

Worker loop'ta:
```cpp
void TileFetcher::WorkerLoop() {
    // ...
    // 1. Cache check (worker thread'de)
    std::vector<uint8_t> cachedData;
    if (request.tryReadCache && request.tryReadCache(request.key, cachedData)) {
        result.data = std::move(cachedData);
        result.success = true;
        result.httpStatus = 200;  // KRITIK: State machine uyumu için
        // HTTP skip, direkt callback → FetchOk → DecodeOk akışı korunur
    } else {
        // 2. HTTP fetch
        result.success = DoFetch(request, result);
        // 3. Cache write
        if (result.success && request.writeCache) {
            request.writeCache(request.key, result.data);
        }
    }
}
```

**Beklenen Kazanım:** Main thread disk I/O = 0

### P1.3 CURL Connection Pooling (Thread-Local + Proper Reset)

#### Mevcut Sorun (`tile_fetcher.cpp:132-170`)
Her fetch için `curl_easy_init()` + `curl_easy_cleanup()` = TCP+TLS overhead

#### Yeni Tasarım
```cpp
// Thread-local CURL handle with proper lifecycle
class TileFetcher {
    // Her worker kendi handle'ını kullanır
    static thread_local CURL* tls_curl_;
    static thread_local struct curl_slist* tls_headers_;  // Header list
    
    CURL* GetCurlHandle() {
        if (!tls_curl_) {
            tls_curl_ = curl_easy_init();
            // One-time persistent setup
            curl_easy_setopt(tls_curl_, CURLOPT_TCP_KEEPALIVE, 1L);
            curl_easy_setopt(tls_curl_, CURLOPT_TCP_KEEPIDLE, 120L);
            curl_easy_setopt(tls_curl_, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(tls_curl_, CURLOPT_SSL_VERIFYPEER, 0L);
        }
        return tls_curl_;
    }
    
    bool DoFetch(const FetchRequest& request, FetchResult& result) {
        CURL* curl = GetCurlHandle();
        
        // KRITIK: curl_easy_reset() per-request (connection reuse korunur)
        curl_easy_reset(curl);
        
        // Re-apply persistent options (reset sonrası gerekli)
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        // ... diğer sabit ayarlar
        
        // Per-request ayarlar
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.data);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, DOWNLOAD_TIMEOUT_SEC);
        
        // Header list (her request için yeniden oluştur)
        if (tls_headers_) {
            curl_slist_free_all(tls_headers_);  // Önceki listeyi temizle
            tls_headers_ = nullptr;
        }
        tls_headers_ = curl_slist_append(nullptr, ("Origin: " + origin).c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, tls_headers_);
        
        CURLcode res = curl_easy_perform(curl);
        // ... sonuç işleme
    }
    
    // Thread exit cleanup (WorkerLoop sonunda)
    void CleanupThreadLocal() {
        if (tls_headers_) {
            curl_slist_free_all(tls_headers_);
            tls_headers_ = nullptr;
        }
        if (tls_curl_) {
            curl_easy_cleanup(tls_curl_);
            tls_curl_ = nullptr;
        }
    }
};

// WorkerLoop sonunda:
void TileFetcher::WorkerLoop() {
    while (running_) {
        // ... fetch logic
    }
    CleanupThreadLocal();  // Thread çıkışında cleanup
}
```

**Beklenen Kazanım:** %50-70 fetch hızı (connection reuse)

### P1.4 Cancel Hook (Progress Callback)

**TLS Key Lifecycle:**
1. `WorkerLoop` başında `tls_currentKey_` set edilir
2. `curl_easy_perform()` sırasında `ProgressCallback` bu key'i okur
3. Fetch sonrası `tls_currentKey_` invalidate edilir (optional ptr veya sentinel)

```cpp
// Thread-local current key for cancel check
static thread_local std::optional<TileKey> tls_currentKey_;

// Cancel için progress callback
int ProgressCallback(void* userp, curl_off_t dltotal, curl_off_t dlnow, 
                     curl_off_t ultotal, curl_off_t ulnow) {
    auto* fetcher = static_cast<TileFetcher*>(userp);
    
    if (!tls_currentKey_.has_value()) return 0;  // Guard
    
    std::lock_guard<std::mutex> lock(fetcher->cancelMutex_);
    if (fetcher->cancelled_.count(*tls_currentKey_)) {
        fetcher->cancelled_.erase(*tls_currentKey_);  // Temizle
        return 1;  // Abort transfer
    }
    return 0;  // Continue
}

// WorkerLoop içinde:
void TileFetcher::WorkerLoop() {
    while (running_) {
        // ... queue'dan request al
        
        tls_currentKey_ = request.key;  // SET: fetch öncesi
        
        result.success = DoFetch(request, result);
        
        tls_currentKey_.reset();  // INVALIDATE: fetch sonrası
        
        // ... callback çağrısı
    }
    CleanupThreadLocal();
}

// DoFetch içinde:
curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);  // KRITIK: Progress callback için
curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
```

### Dosya Değişiklikleri
| Dosya | Değişiklik |
|-------|------------|
| `src/io/tile_url_template.h` | Yeni dosya |
| `src/io/tile_url_template.cpp` | Yeni dosya |
| `src/io/download_types.h` | Cache callbacks ekleme |
| `src/io/tile_fetcher.cpp` | Cache check in worker, CURL pooling |
| `src/scheduling/tile_scheduler.cpp` | BuildUrl → TileUrlTemplate, cache callback setup |
| `CMakeLists.txt` | Yeni kaynak dosyalar |

### Kabul Kriterleri
- [ ] `TileScheduler::Request` içinde disk I/O yok
- [ ] URL oluşturma regexsiz
- [ ] CURL handle'lar reuse ediliyor (connection: keep-alive)

---

## P2 — Fetcher/Decoder Priority + Lock Azaltma

### Hedef
Urgent leaf tile'ların prefetch arkasında kalmaması ve callback contention düşmesi.

### P2.1 Decoder Priority Queue

#### Mevcut Sorun
`TileDecoder` FIFO queue kullanıyor → Urgent tile prefetch arkasında kalabilir

#### Yeni Tasarım
```cpp
// src/io/tile_decoder.h
struct DecodeRequest {
    TileKey key;
    std::vector<uint8_t> data;
    Priority priority = Priority::Normal;
    float score = 0.0f;
};

struct DecodeRequestCompare {
    bool operator()(const DecodeRequest& a, const DecodeRequest& b) const {
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.score < b.score;
    }
};

class TileDecoder {
    std::priority_queue<DecodeRequest, std::vector<DecodeRequest>, DecodeRequestCompare> queue_;
};
```

### P2.2 Callback Lock-Free Pattern

#### Mevcut Sorun
```cpp
// Lock altında callback çağrısı = contention
std::lock_guard<std::mutex> lock(callbackMutex_);
if (resultCallback_) {
    resultCallback_(std::move(result));  // Uzun sürebilir
}
```

#### Yeni Tasarım
```cpp
// Lock dışında callback çağrısı
ResultCallback callbackCopy;
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callbackCopy = resultCallback_;
}
if (callbackCopy) {
    callbackCopy(std::move(result));
}
```

### P2.3 Priority Starvation Önleme

```cpp
// Decode/Upload queue'da fairness kuralı
class PriorityQueueWithFairness {
    std::priority_queue<...> urgentQueue_;
    std::queue<...> normalQueue_;
    int urgentProcessed_ = 0;
    static constexpr int URGENT_BATCH_SIZE = 4;  // Her 4 urgent sonra 1 normal
    
public:
    bool Pop(T& item) {
        // Fairness: urgent batch sonrası normal'a şans ver
        if (urgentProcessed_ >= URGENT_BATCH_SIZE && !normalQueue_.empty()) {
            item = std::move(normalQueue_.front());
            normalQueue_.pop();
            urgentProcessed_ = 0;
            return true;
        }
        
        // Urgent öncelikli
        if (!urgentQueue_.empty()) {
            item = std::move(urgentQueue_.top());
            urgentQueue_.pop();
            ++urgentProcessed_;
            return true;
        }
        
        // Normal fallback
        if (!normalQueue_.empty()) {
            item = std::move(normalQueue_.front());
            normalQueue_.pop();
            urgentProcessed_ = 0;
            return true;
        }
        
        return false;
    }
};
```

### Dosya Değişiklikleri
| Dosya | Değişiklik |
|-------|------------|
| `src/io/tile_decoder.h` | Priority/score ekleme, priority_queue |
| `src/io/tile_decoder.cpp` | Queue değişikliği, callback pattern |
| `src/io/tile_fetcher.cpp` | Callback pattern |
| `src/scheduling/tile_scheduler.cpp` | Decode'a priority/score taşıma |

### Kabul Kriterleri
- [ ] Urgent tile decode'ları prefetch'i bypass eder
- [ ] Callback mutex contention düşer (telemetri ile ölç)

---

## P3 — Scheduler Backpressure (Drop→Fail Döngüsü Kaldırma)

### Hedef
Result queue overflow'unu drop yerine backpressure ile yönetmek.

### P3.1 Bounded Queue + Condition Variable (Shutdown-Safe)

#### Mevcut Sorun (`tile_scheduler.cpp:183-199`)
```cpp
if (fetchResults_.size() >= MAX_RESULT_QUEUE) {
    // DROP oldest → tile Failed → retry → wasted cycles
    fetchResults_.pop();
    ++droppedFetchResults_;
}
```

#### Yeni Tasarım (Deadlock-Safe)
```cpp
template<typename T>
class BoundedQueue {
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    size_t maxSize_;
    std::atomic<bool> closed_{false};  // KRITIK: Shutdown flag
    
public:
    // Shutdown için - bekleyen tüm thread'leri uyandırır
    void Close() {
        closed_ = true;
        notFull_.notify_all();
        notEmpty_.notify_all();
    }
    
    // Push - closed ise false döner (deadlock önleme)
    bool Push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this] { 
            return queue_.size() < maxSize_ || closed_; 
        });
        if (closed_) return false;  // Producer çık
        queue_.push(std::move(item));
        notEmpty_.notify_one();
        return true;
    }
    
    bool TryPop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        notFull_.notify_one();
        return true;
    }
    
    bool IsClosed() const { return closed_; }
};
```

### P3.2 In-Flight Limit

```cpp
// TileScheduler::Request içinde
int inFlight = pendingFetches_.size() + fetcher_->GetActiveCount();
if (inFlight >= config_.maxInFlightFetches && priority != Priority::Urgent) {
    return;  // Backpressure - dispatch etme
}
```

### Dosya Değişiklikleri
| Dosya | Değişiklik |
|-------|------------|
| `src/core/bounded_queue.h` | Yeni dosya |
| `src/scheduling/tile_scheduler.h` | BoundedQueue kullanımı |
| `src/scheduling/tile_scheduler.cpp` | Drop logic kaldırma, backpressure |
| `src/scheduling/tile_state_machine.cpp` | Drop event kaldırma (opsiyonel) |
| `src/core/config.h` | `maxInFlightFetches` ekleme |

### Kabul Kriterleri
- [ ] `droppedFetchResults_` ve `droppedDecodeResults_` = 0
- [ ] "Failed→retry" sayısı queue overflow kaynaklı artmaz

---

## P4 — Texture Upload Optimizasyonu

### Hedef
GPU upload sırasında hitch'i azaltmak.

### P4.1 Upload Priority Queue

```cpp
// texture_manager.h
struct UploadJob {
    TileKey key;
    Priority priority;
    float score;
};

struct UploadJobCompare {
    bool operator()(const UploadJob& a, const UploadJob& b) const {
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.score < b.score;
    }
};

std::priority_queue<UploadJob, std::vector<UploadJob>, UploadJobCompare> uploadQueue_;
```

### P4.2 Texture Reuse (glTexSubImage2D)

```cpp
// TextureManager::ProcessUploads
if (tile.textureId != 0 && tile.ownsTexture &&
    tile.texWidth == tile.pixelWidth && tile.texHeight == tile.pixelHeight) {
    // Reuse existing texture
    glBindTexture(GL_TEXTURE_2D, tile.textureId);
    
    // KRITIK: Row alignment ayarı (non-power-of-2 için)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 
                    tile.pixelWidth, tile.pixelHeight,
                    GL_RGBA, GL_UNSIGNED_BYTE, tile.pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // Reset to default
} else {
    // Create new texture
    GLuint texId = CreateTexture(tile.pixels.data(), tile.pixelWidth, tile.pixelHeight);
    tile.textureId = texId;
    tile.texWidth = tile.pixelWidth;   // KRITIK: Boyut kaydet
    tile.texHeight = tile.pixelHeight;
    tile.ownsTexture = true;
}
```

### P4.3 Memory Churn Fix

```cpp
// Tile::ClearPixels - shrink_to_fit kaldır
void ClearPixels() {
    pixels.clear();  // shrink_to_fit() yok - capacity korunur
}
```

### P4.4 Deferred Mipmap (Opsiyonel)

```cpp
// İlk frame'de mipmap generate etme, sonraki idle frame'de yap
if (!tile.mipmapGenerated && idleTime > threshold) {
    glBindTexture(GL_TEXTURE_2D, tile.textureId);
    glGenerateMipmap(GL_TEXTURE_2D);
    tile.mipmapGenerated = true;
}
```

### Dosya Değişiklikleri
| Dosya | Değişiklik |
|-------|------------|
| `src/rendering/texture_manager.h` | UploadJob struct, priority queue |
| `src/rendering/texture_manager.cpp` | Priority queue, texture reuse |
| `src/core/tile.h` | texWidth/texHeight, mipmapGenerated |
| `src/scheduling/tile_scheduler.cpp` | QueueUpload'a priority/score taşı |

### Kabul Kriterleri
- [ ] Upload queue'da urgent tile'lar öne geçer
- [ ] Texture recreate sayısı düşer
- [ ] Frame hitch (p99) azalır

---

## P5 — Async Mesh Pipeline (KRİTİK)

### Hedef
"SYNC LEAF MESH" kaynaklı main-thread hitch'ini bitirmek.

### Mevcut Altyapı Analizi

**Mevcut JobSystem** (`globe_engine.h:102`, `job_system.cpp`):
- Main-thread'de `ProcessCount()` ile çalışıyor
- `QueueMeshRebuild()` → `jobSystem_.Submit()` → main-thread execute
- **SORUN:** Job'lar main-thread'de execute edildiği için hitch devam ediyor

**JobSystem Gelecek Kullanımı:**
> **NOT:** P5 sonrası `JobSystem` aktif kullanımdan çıkar. Şu an sadece mesh rebuild için
> kullanılıyor ve `TileMeshScheduler`'a devredilecek. Gelecekte DEM processing,
> vector tile parsing veya diğer CPU-bound işler için yeniden aktif edilebilir.
> Şimdilik **korunur ama kullanılmaz** (dead code değil, reserve).

**P5 Stratejisi:**
1. Mevcut `JobSystem` **korunur** (gelecek kullanım için reserve)
2. `TileMeshScheduler` **yeni async worker pool** olarak eklenir
3. `QueueMeshRebuild()` → `TileMeshScheduler::Request()` yönlendirilir
4. `ProcessMeshRebuildQueue()` → `TileMeshScheduler::TryGetResult()` loop

**Veri Yarışı Önleme:**
- `rebuildPending_` set'i korunur (duplicate request prevention)
- `meshRevision` ile stale result detection
- Worker'lar tile map'e doğrudan erişmez (snapshot extent/edgeMask ile çalışır)

### P5.1 TileMeshScheduler Modülü

```cpp
// src/rendering/tile_mesh_scheduler.h
struct MeshRequest {
    TileKey key;
    Extent extent;
    uint8_t edgeMask;
    int revision;  // Stale check için
    Priority priority;
    float score;
};

struct MeshResult {
    TileKey key;
    int revision;
    std::vector<float> vertices;
    // indices: Shared EBO kullanılıyorsa BOŞ, aksi halde dolu
    std::vector<unsigned int> indices;  // empty when useSharedEBO=true
    bool useSharedEBO = true;           // MeshTemplate shared index buffer
    bool demUsed;
    bool demPending;
};

// Worker build logic:
if (config.useSharedEBO && MeshTemplate::Exists(segments)) {
    result.useSharedEBO = true;
    result.indices.clear();  // Shared EBO kullanılacak, indices taşıma
} else {
    result.useSharedEBO = false;
    result.indices = BuildIndices(segments);  // Per-tile indices
}

class TileMeshScheduler {
public:
    explicit TileMeshScheduler(int numWorkers = 4);
    ~TileMeshScheduler();
    
    void Request(const MeshRequest& request);
    bool TryGetResult(MeshResult& result);
    int GetPendingCount() const;
    
    void SetDemManager(DemManager* dem);
    
private:
    void WorkerLoop();
    
    std::priority_queue<MeshRequest, ...> requestQueue_;
    BoundedQueue<MeshResult> resultQueue_;
    std::vector<std::thread> workers_;
    DemManager* demManager_ = nullptr;
};
```

### P5.2 Shared Index Buffer (MeshTemplate)

```cpp
// src/rendering/mesh_template.h
class MeshTemplate {
public:
    // Singleton per segment count
    static MeshTemplate& Get(int segments);
    
    GLuint GetSharedEBO() const { return sharedEbo_; }
    int GetIndexCount() const { return indexCount_; }
    int GetSkirtIndexCount() const { return skirtIndexCount_; }
    
private:
    GLuint sharedEbo_ = 0;
    int indexCount_ = 0;       // Main grid
    int skirtIndexCount_ = 0;  // Skirt triangles
    
    void CreateIndices(int segments);
    
    // KRITIK: Destructor'da silinmez (singleton lifetime)
    // Eviction'da tile.ebo = 0 set edilir ama shared EBO silinmez
};
```

**Ownership Kuralları:**
- `MeshTemplate` singleton, EBO ownership **tamamen MeshTemplate'e ait**
- `Tile.ebo` = 0 (shared kullanıldığında)
- `Tile.ownsEBO` flag eklenir (eviction'da kontrol)
- Eviction: `if (tile.ownsEBO && tile.ebo != 0) glDeleteBuffers(...)`

```cpp
// Tile struct'a ekle
struct Tile {
    // ...
    bool ownsEBO = true;  // false when using MeshTemplate shared EBO
};
```

Per-tile sadece vertex VBO upload → %40 GPU upload azalır

### P5.3 GlobeEngine Entegrasyonu

**meshRevision Increment Kuralları:**
```cpp
// Tile struct'ta:
int meshRevision = 0;  // Monotonic counter

// Increment tetikleyicileri (Update() içinde):
// 1. Edge mask değiştiğinde:
if (tile.edgeCoarserMask != tile.prevEdgeCoarserMask) {
    tile.meshRevision++;
    tile.prevEdgeCoarserMask = tile.edgeCoarserMask;
}

// 2. DEM verisi geldiğinde:
if (tile.demPending && demManager_->HasData(key)) {
    tile.meshRevision++;
    tile.demPending = false;
}

// 3. Tile extent/segments değiştiğinde (nadiren):
if (tile.extent != newExtent || config.meshSegments != tile.builtSegments) {
    tile.meshRevision++;
}

// Stale result check:
if (meshResult.revision != tile.meshRevision) {
    // Discard - tile state değişmiş, yeni request gerekli
    continue;
}
```

```cpp
// globe_engine.cpp - Update() değişikliği

// KALDIRILACAK:
// for (const TileKey& key : selection.leafSet) {
//     if (!it->second.hasMesh) {
//         BuildTileMesh(it->second);  // SYNC - kaldır
//     }
// }

// YENİ:
for (const TileKey& key : selection.leafSet) {
    auto it = tiles_.find(key);
    if (it != tiles_.end() && !it->second.hasMesh && !it->second.meshPending) {
        MeshRequest req;
        req.key = key;
        req.extent = it->second.extent;
        req.edgeMask = it->second.edgeCoarserMask;
        req.revision = it->second.meshRevision;
        req.priority = Priority::Urgent;
        req.score = it->second.importance;
        meshScheduler_->Request(req);
        it->second.meshPending = true;
    }
}

// Process mesh results
MeshResult meshResult;
while (meshScheduler_->TryGetResult(meshResult)) {
    auto it = tiles_.find(meshResult.key);
    if (it != tiles_.end() && it->second.meshRevision == meshResult.revision) {
        TileMeshBuilder::UploadToGPU(it->second, meshResult);
        it->second.meshPending = false;
    }
}
```

### Dosya Değişiklikleri
| Dosya | Değişiklik |
|-------|------------|
| `src/rendering/tile_mesh_scheduler.h` | Yeni dosya |
| `src/rendering/tile_mesh_scheduler.cpp` | Yeni dosya |
| `src/rendering/mesh_template.h` | Yeni dosya |
| `src/rendering/mesh_template.cpp` | Yeni dosya |
| `src/core/tile.h` | meshPending, meshRevision |
| `src/engine/globe_engine.h` | meshScheduler_ member |
| `src/engine/globe_engine.cpp` | Async mesh entegrasyonu |
| `src/rendering/tile_mesh_builder.cpp` | Shared EBO kullanımı |
| `CMakeLists.txt` | Yeni kaynak dosyalar |

### Kabul Kriterleri
- [ ] Leaf mesh build main-thread'de yapılmaz
- [ ] Frame stutter (p99) önemli ölçüde düşer
- [ ] Placeholder/fallback sayısı artmamalı (gap-free korunur)

---

## P6 — Pin/Eviction & Render Micro-Opt

### Hedef
Alloc ve GL delete spike azaltma.

### P6.1 Pin Set Kopyasını Kaldır

```cpp
// Tile struct'a ekle
struct Tile {
    // ...
    uint32_t pinnedEpoch = 0;  // Hangi frame'de pinned
};

// TextureManager
uint32_t currentEpoch_ = 0;

void TextureManager::PinForFrame(Tile& tile) {
    tile.pinnedEpoch = currentEpoch_;
}

void TextureManager::AdvanceEpoch() {
    ++currentEpoch_;
}

bool TextureManager::IsPinnedThisFrame(const Tile& tile) const {
    return tile.pinnedEpoch == currentEpoch_;
}
```

### P6.2 Eviction Budget

```cpp
void TextureManager::EvictIfNeeded(TileMap& tiles, int maxTiles) {
    constexpr int MAX_EVICTS_PER_FRAME = 8;
    constexpr double EVICT_TIME_BUDGET_MS = 1.0;
    
    double startTime = glfwGetTime() * 1000.0;
    int evictCount = 0;
    
    // nth_element yerine partial_sort (top N oldest)
    // ...
    
    while (evictCount < MAX_EVICTS_PER_FRAME && 
           (glfwGetTime() * 1000.0 - startTime) < EVICT_TIME_BUDGET_MS) {
        // Evict one
        ++evictCount;
    }
}
```

### P6.3 Render Micro-Opt

```cpp
void TileRenderer::BeginBatch(const glm::mat4& mvp, bool wireframe) {
    // glActiveTexture bir kez
    glActiveTexture(GL_TEXTURE0);
    // ...
}

void TileRenderer::RenderTile(const Tile& tile) {
    // glBindVertexArray(0) kaldır - EndBatch'te yapılacak
    glBindVertexArray(tile.vao);
    glDrawElements(...);
    // glBindVertexArray(0) yok
}

void TileRenderer::EndBatch() {
    glBindVertexArray(0);  // Batch sonunda bir kez
}
```

### Dosya Değişiklikleri
| Dosya | Değişiklik |
|-------|------------|
| `src/core/tile.h` | pinnedEpoch |
| `src/rendering/texture_manager.h` | Epoch-based pinning |
| `src/rendering/texture_manager.cpp` | Eviction budget |
| `src/rendering/tile_renderer.cpp` | VAO bind optimization |

### Kabul Kriterleri
- [ ] Eviction spike'ları azalır
- [ ] Render batch daha stabil
- [ ] Alloc sayısı azalır

---

## Public API / Interface Değişiklikleri Özeti

| Modül | Değişiklik |
|-------|------------|
| `FetchRequest` | `tryReadCache`, `writeCache` callbacks |
| `DecodeRequest` | `Priority priority`, `float score` |
| `TextureManager` | `UploadJob` struct (priority/score) |
| `Tile` | `pinnedEpoch`, `meshPending`, `meshRevision`, `texWidth/texHeight` |
| **Yeni Modüller** | |
| `TileUrlTemplate` | `src/io/tile_url_template.{h,cpp}` |
| `TileMeshScheduler` | `src/rendering/tile_mesh_scheduler.{h,cpp}` |
| `MeshTemplate` | `src/rendering/mesh_template.{h,cpp}` |
| `BoundedQueue` | `src/core/bounded_queue.h` |
| `FrameTimeTracker` | `src/core/frame_time_tracker.h` |

---

## Test & Doğrulama Senaryoları

### T1: Hitch Testi
- 30 saniye hızlı pan/zoom
- **Beklenen:** p95/p99 frame-time düşmeli

### T2: Latency Testi
- Request→Ready median/90p ölçümü
- **Beklenen:** Median %30+ düşüş

### T3: Stress Testi
- maxZoom yüksekken zoom in/out
- **Beklenen:** Queue overflow olmamalı, drop = 0

### T4: Correctness Testi
- Render gap metrikleri (missing/fallback/placeholder)
- **Beklenen:** Artmamalı (gap-free korunur)

---

## Varsayımlar & Kısıtlamalar

- Hedef: **OpenGL 3.3 core** (PBO optional, default kapalı)
- Görsel kalite korunur (mipmap/placeholder/fallback davranışı değişmez)
- Öncelik: **Frame hitch azaltma** > throughput
- Thread-safe değişiklikler mevcut mutex pattern'ini takip eder

---

## Faz Tamamlama Protokolü

Her faz tamamlandığında:
1. `docs/API_PORT_REVIEW_PROMPT.md` → **Faz Tamamlama Günlüğü** güncellenir
2. `docs/API_PORT_REVIEW_PROMPT.md` → **Güncel Durum Snapshot** güncellenir
3. Test senaryoları çalıştırılır ve sonuçlar kaydedilir

---

## Uygulama Sırası

```
P0 (Telemetri) ─────────────────────────────────────────────────────►
         │
         ▼
P1 (Fetch/Cache) ──────────────────────────────────────────────────►
         │
         ├──► P2 (Priority/Lock) ──────────────────────────────────►
         │
         └──► P3 (Backpressure) ───────────────────────────────────►
                      │
                      ▼
              P4 (Texture Upload) ─────────────────────────────────►
                      │
                      ▼
              P5 (Async Mesh) ◄──── KRİTİK ────────────────────────►
                      │
                      ▼
              P6 (Micro-Opt) ──────────────────────────────────────►
```

**Bağımlılıklar:**
- P0 → Tüm fazlar için önkoşul
- P1 → Bağımsız
- P2 → P1 ile paralel olabilir
- P3 → P2 sonrası (priority queue gerekli)
- P4 → P3 sonrası (backpressure pattern kullanır)
- P5 → P4 sonrası (upload pattern'ini takip eder)
- P6 → Son (diğer fazlar stabil olduktan sonra)
