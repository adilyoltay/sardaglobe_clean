# Globe Engine Refactoring Plan

## Overview

`globe_engine.cpp` is currently 430KB (11,500+ lines) and handles too many responsibilities.
This document outlines a plan to refactor it into smaller, focused modules.

## Current Responsibilities in globe_engine.cpp

1. **Tile Synchronization** (~1500 lines)
   - `SyncRasterTiles()`, `SyncRasterTilesPass3()`, `SyncLayerTiles()`, `SyncVectorTiles()`
   - Tile creation, loading, eviction

2. **Download Management** (~800 lines)
   - `WorkerLoop()`, `DemWorkerLoop()`
   - Download queue management
   - Result processing

3. **Rendering** (~1200 lines)
   - `Render()`, `RenderTiles()`, `RenderRasterOverlays()`, `RenderVectors()`
   - Shader management, GL state

4. **Camera & Navigation** (~600 lines)
   - Camera matrix calculation
   - Mouse/keyboard input handling
   - Animation updates

5. **DEM/Terrain** (~1500 lines)
   - Mesh stitching logic
   - Height sampling
   - DEM tile management

6. **UI (ImGui)** (~800 lines)
   - Debug panels
   - Statistics display

7. **Utilities** (~500 lines)
   - URL building
   - Coordinate conversions
   - Tile math

## Proposed Module Structure

```
src/
├── globe_engine.cpp          # Core engine, reduced to ~2000 lines
├── globe_engine.h            # Public API
├── globe_engine_impl.h       # Internal state (Impl struct)
│
├── tile_sync.cpp             # NEW: Tile synchronization logic
├── tile_sync.h
│
├── download_manager.cpp      # NEW: Download queue and workers
├── download_manager.h
│
├── globe_renderer.cpp        # NEW: Rendering pipeline
├── globe_renderer.h
│
├── dem_manager.cpp           # NEW: DEM/terrain handling
├── dem_manager.h
│
├── texture_manager.cpp       # DONE: Already extracted
├── texture_manager.h
│
├── tile_manager.cpp          # EXISTS: Expand with eviction logic
├── tile_manager.h
│
├── tile_scheduler.cpp        # EXISTS: Keep as-is
├── tile_scheduler.h
```

## Refactoring Priority

### Phase 1: Low-Risk Extractions (DONE)
- [x] TextureManager extracted
- [x] Lock hierarchy documented
- [x] Deprecated code marked

### Phase 2: Download Manager
- [ ] Extract `WorkerLoop()` and download queue management
- [ ] Create `DownloadManager` class with:
  - `Enqueue()`, `Cancel()`, `ProcessResults()`
  - Internal thread pool
  - Priority queue management

### Phase 3: Tile Sync
- [ ] Extract `SyncRasterTiles()` family to `tile_sync.cpp`
- [ ] Reduce coupling to Impl struct
- [ ] Create clear interface for tile state updates

### Phase 4: DEM Manager
- [ ] Extract DEM stitching logic
- [ ] Extract `DemWorkerLoop()`
- [ ] Create `DemManager` class

### Phase 5: Renderer
- [ ] Extract `Render*()` functions
- [ ] Create `GlobeRenderer` class
- [ ] Manage shader programs internally

## Implementation Notes

### Dependency Order
When extracting modules, follow this order to minimize circular dependencies:
1. TextureManager (no deps) ✅
2. DownloadManager (depends on TextureManager)
3. TileSync (depends on DownloadManager)
4. DemManager (depends on TileSync)
5. GlobeRenderer (depends on all above)

### Interface Design
Each extracted module should:
- Have a clear, minimal public API
- Use dependency injection (pass managers via constructor)
- Not directly access `GlobeEngine::Impl`
- Use callbacks or events for cross-module communication

### Testing Strategy
- Add unit tests for each extracted module
- Create mock implementations for dependencies
- Verify no regression in rendering quality

## Estimated Effort

| Phase | Estimated Lines | Complexity | Risk |
|-------|-----------------|------------|------|
| Phase 1 | ~500 | Low | Low |
| Phase 2 | ~800 | Medium | Medium |
| Phase 3 | ~1500 | High | Medium |
| Phase 4 | ~1500 | High | High |
| Phase 5 | ~1200 | Medium | Medium |

Total: ~5500 lines to extract, reducing globe_engine.cpp to ~6000 lines.

## Success Criteria

1. No single source file > 3000 lines
2. Each module has single responsibility
3. Clear ownership of mutex locks per module
4. Unit tests for extracted modules
5. No performance regression
