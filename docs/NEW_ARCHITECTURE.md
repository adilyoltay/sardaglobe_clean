# Native Globe - Yeni Temiz Mimari

## Hedef
- `globe_engine.cpp` (420KB, 11K+ satır) → Modüler, test edilebilir bileşenler
- Her sınıf tek sorumluluk (Single Responsibility)
- Bağımlılık enjeksiyonu (Dependency Injection)
- Thread-safe tasarım

## Dosya Yapısı

```
src/
├── core/                      # Temel tipler
│   ├── tile_key.h            # TileKey struct (mevcut, korunacak)
│   ├── tile.h                # Tile struct (sadeleştirilecek)
│   ├── globe_config.h        # Configuration (mevcut, korunacak)
│   └── constants.h           # Globe sabitleri (RADIUS, vb.)
│
├── math/                      # Matematik yardımcıları
│   ├── tile_math.h           # Tile geometry (mevcut, korunacak)
│   ├── frustum.h             # Frustum culling
│   └── coordinate_utils.h    # Lat/Lon dönüşümleri
│
├── io/                        # I/O işlemleri
│   ├── tile_fetcher.h/cpp    # HTTP download (ITileFetcher)
│   ├── tile_decoder.h/cpp    # Image decode (ITileDecoder)
│   ├── tile_cache.h/cpp      # Disk cache yönetimi
│   └── download_types.h      # DownloadJob, DownloadResult
│
├── scheduling/                # Tile lifecycle
│   ├── tile_scheduler.h/cpp  # Fetch/Decode orchestration
│   ├── tile_lod_selector.h/cpp # LOD selection (SSE-based)
│   └── tile_priority.h       # Priority hesaplama
│
├── rendering/                 # OpenGL rendering
│   ├── tile_renderer.h/cpp   # Tile mesh rendering
│   ├── shader_manager.h/cpp  # Shader compile/manage
│   ├── texture_manager.h/cpp # Texture upload/evict
│   └── gl_state.h            # GL state tracking
│
├── camera/                    # Kamera sistemi
│   ├── earth_camera.h/cpp    # (mevcut, korunacak)
│   └── flight_controller.h/cpp # (mevcut, korunacak)
│
├── engine/                    # Ana motor
│   ├── globe_engine.h/cpp    # Orchestrator (küçük!)
│   └── frame_loop.h          # Update/Render ayrımı
│
└── api/                       # Public API
    ├── globe_api.h/cpp       # (mevcut, sadeleştirilecek)
    └── value.h               # (mevcut, korunacak)
```

## Bileşen Sorumlulukları

### 1. TileFetcher (io/)
- HTTP GET requests
- Timeout/retry handling
- Origin/Referer headers
- Thread-safe queue

### 2. TileDecoder (io/)
- PNG/JPG → RGBA decode
- Worker thread pool
- Memory-efficient

### 3. TileScheduler (scheduling/)
- Tile lifecycle state machine
- Fetch → Decode → Upload pipeline
- Concurrency limits
- Priority queue

### 4. TileLodSelector (scheduling/)
- SSE-based LOD selection
- Frustum culling
- Horizon culling
- Fallback logic

### 5. TextureManager (rendering/)
- GPU texture upload (time-budgeted)
- LRU eviction
- Texture pooling

### 6. TileRenderer (rendering/)
- Mesh generation
- Draw calls
- Skirt generation

### 7. GlobeEngine (engine/)
- Sadece orchestration
- Update() → LOD select, schedule
- Render() → Draw calls only

## Frame Loop

```cpp
void GlobeEngine::Frame(float dt) {
    // Phase 1: Input & Camera
    ProcessInput();
    camera.Update(dt);
    
    // Phase 2: Tile Selection (CPU)
    auto selection = lodSelector.Select(camera, frustum);
    
    // Phase 3: Resource Scheduling (CPU)
    scheduler.ProcessSelection(selection);
    scheduler.Update();
    
    // Phase 4: GPU Upload (time-budgeted)
    textureManager.ProcessUploads(MAX_UPLOAD_TIME_MS);
    
    // Phase 5: Render (GPU)
    renderer.Begin(camera);
    renderer.DrawTiles(selection.leaves);
    renderer.End();
    
    // Phase 6: Eviction
    textureManager.EvictIfNeeded(MAX_TILES);
}
```

## Migration Stratejisi

1. **Faz 1**: Core types'ları ayır (tile_key, constants)
2. **Faz 2**: I/O modüllerini oluştur (fetcher, decoder, cache)
3. **Faz 3**: Scheduling modüllerini oluştur
4. **Faz 4**: Rendering modüllerini oluştur
5. **Faz 5**: GlobeEngine'i küçült (orchestrator only)
6. **Faz 6**: Test ve doğrulama
