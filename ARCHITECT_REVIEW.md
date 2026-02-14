# Architecture Review Report

## Executive Summary
The `sardaglobe` codebase has made significant progress towards Google Earth parity. Most of the critical architectural pillars (RTE, PBO, Horizon Culling) are implemented. The "Catastrophic Rendering Failure" described (spikes, holes) has been addressed with corrective logic in `TileMeshBuilder` and `TerrainRGBDecoder`, although some features remain disabled by default in `config.h`.

## Detailed Status by Phase

### 🔴 Phase 1: Geometry Recovery [COMPLETE]
**Status:** ✅ **Implemented**
- **Skirt Generation:** `TileMeshBuilder::GenerateSkirts` correctly projects vertices inward along the radial direction (surface normal) relative to the RTE origin. Skirt depth is dynamic based on zoom level (`ComputeGeometricError`) and terrain relief.
- **DEM No-Data:** `TerrainRGBDecoder` includes `SanitizeTerrainHeights` which clamps values below `demNoDataMinHeightM` (-11000m) to 0. Enabled by default via `forceClampTerrainNoData`.
- **Bounding Box Culling:** `Tile` extent and bounding radius are computed analytically from WGS84 coordinates (`TileBoundingRadius` in `tile_math.h`), independent of the generated mesh artifacts. This prevents "spikes" from corrupting the culling volume.

### 🟡 Phase 2: Precision & Z-Buffer [PARTIAL]
**Status:** ⚠️ **Implemented but Disabled**
- **RTE (Relative To Eye):** ✅ **Active**. `TileMeshBuilder` splits positions into High/Low components. `TileRenderer` binds these as uniforms. `useRteRender` is `true` by default.
- **Reversed-Z:** ⚠️ **Inactive**. Logic exists in `GlobeEngine` and `PerspectiveCamera` (infinite far plane matrix), but `reversedZEnabled` is `false` in `config.h`. Needs enablement to solve Z-fighting.

### 🟠 Phase 3: Async & Array Architecture [PARTIAL]
**Status:** ⚠️ **Mixed**
- **PBO Uploads:** ✅ **Active**. `PboUploadManager` is fully integrated with fence synchronization (`GL_SYNC_GPU_COMMANDS_COMPLETE`). `usePboUploads` is `true` by default.
- **Texture Arrays:** ⚠️ **Inactive**. `TextureArrayManager` and shader support exist (`sampler2DArray`), but `useTexture2DArray` is `false` in `config.h`.
- **Persistent Cache:** ⚠️ **Basic**. `NodeDataDiskCache` and `TileCache` implement filesystem-based caching. While not the suggested SQLite/LevelDB, it provides persistence (L3).

### 🔵 Phase 4: Advanced Optimizations [PARTIAL]
**Status:** ⚠️ **Mixed**
- **Horizon Culling:** ✅ **Active**. `HorizonCuller` is integrated into `LodSelector`. `useHorizonCulling` is `true`.
- **GPU Displacement:** ⚠️ **Inactive**. Supported in shaders and `TileRenderer` (`RenderTileWithHeightmap`), but `terrainDisplacementMode` defaults to `CPU_MESH_BAKE`.
- **Stochastic Dithering:** ❌ **Missing**. Shader generation (`ShaderManager::BuildFragmentShader`) uses standard alpha mixing (`mix(unpop, child, blend)`). No Bayer matrix or stochastic discard logic found.

## Recommendations
1. **Enable Reversed-Z:** Set `reversedZEnabled = true` in `src/core/config.h`.
2. **Enable Texture Arrays:** Set `useTexture2DArray = true` in `src/core/config.h`.
3. **Implement Dithering:** Replace alpha blending in `ShaderManager` with a dithering pattern to resolve overdraw issues.
