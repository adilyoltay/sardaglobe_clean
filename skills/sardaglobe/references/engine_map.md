# Core Engine Map

1. Tile pyramid and LOD selection
   - `src/scheduling/tile_pyramid.h`
   - `src/scheduling/tile_pyramid.cpp`

2. Tile scheduling and state transitions
   - `src/scheduling/tile_scheduler.h`
   - `src/scheduling/tile_scheduler.cpp`
   - `src/scheduling/job_system.h`
   - `src/scheduling/job_system.cpp`

3. Engine control surface
   - `src/engine/globe_engine.h`
   - `src/engine/globe_engine.cpp`

4. Elevation and DEM
   - `src/io/dem_manager.h`
   - `src/io/dem_manager.cpp`

5. Camera and navigation
   - `src/camera/earth_camera.h`
   - `src/camera/earth_camera.cpp`
   - `src/camera/flight_controller.h`
   - `src/camera/flight_controller.cpp`

6. Rendering pipeline
   - `src/rendering/tile_renderer.h`
   - `src/rendering/tile_renderer.cpp`
   - `src/rendering/render_frame.h`
   - `src/rendering/render_frame.cpp`
   - `src/rendering/shader_manager.h`
   - `src/rendering/shader_manager.cpp`

7. Tests and visual checks
   - `tests/lod_conformance_test.cpp`
   - `tests/visual_lod_test.cpp`
