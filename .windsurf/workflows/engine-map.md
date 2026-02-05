---
description: Core engine dosya haritası ve modül referansı
---

# Core Engine Map

Globe engine'deki ana modüller ve dosya konumları.

## 1. Tile Pyramid ve LOD Selection
- `src/scheduling/tile_pyramid.h`
- `src/scheduling/tile_pyramid.cpp`
- `src/scheduling/lod_selector.h`
- `src/scheduling/lod_selector.cpp`

## 2. Tile Scheduling ve State Transitions
- `src/scheduling/tile_scheduler.h`
- `src/scheduling/tile_scheduler.cpp`
- `src/scheduling/job_system.h`
- `src/scheduling/job_system.cpp`

## 3. Engine Control Surface
- `src/engine/globe_engine.h`
- `src/engine/globe_engine.cpp`

## 4. Elevation ve DEM
- `src/io/dem_manager.h`
- `src/io/dem_manager.cpp`

## 5. Camera ve Navigation
- `src/camera/earth_camera.h`
- `src/camera/earth_camera.cpp`
- `src/camera/flight_controller.h`
- `src/camera/flight_controller.cpp`

## 6. Rendering Pipeline
- `src/rendering/tile_renderer.h`
- `src/rendering/tile_renderer.cpp`
- `src/rendering/render_frame.h`
- `src/rendering/render_frame.cpp`
- `src/rendering/shader_manager.h`
- `src/rendering/shader_manager.cpp`

## 7. Tests
- `tests/lod_conformance_test.cpp` — Numeric parity
- `tests/visual_lod_test.cpp` — Visual regression
