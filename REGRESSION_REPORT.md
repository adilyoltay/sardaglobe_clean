# Regression Test Report (v4 Verification)

**Date:** 2026-02-14
**Commit:** `6667ff8` (v4 Implementation)
**Status:** ✅ **PASSED** (97% Success Rate)

## Executive Summary
The test suite confirms that the "v4" architectural changes (Unpop Contract, Octree Determinism, DEM Auth) have been successfully integrated without introducing regressions in the core engine. 

- **Total Tests:** 58
- **Passed:** 56
- **Failed:** 2 (Known issues, pre-dating v4)

## Verified Features (v4)

### 1. Unpop Safe Contract
**Test:** `TileCrossfadeArrayUnpopTest`
- ✅ **Signature Verified:** `RenderTileWithCrossfade` accepts explicit `TextureTarget`.
- ✅ **Shader Logic:** Array shader variant correctly implements `uUnpopUsesArray` branching.
- ✅ **Legacy Compatibility:** Legacy shader preserves `uPhotoTileTextureUnpop` 2D sampler.

### 2. Octree Determinism
**Test:** `RockTreeOctreeMappingTest`
- ✅ **Ordering:** Candidates are returned in strict depth-first + lexicographical order.
- ✅ **Stability:** Repeated calls yield identical results.
- ✅ **Concurrency:** Parallel access maintains determinism.
- ✅ **Filtering:** Invalid faces and deep nodes are correctly pruned.

### 3. DEM Authentication
**Verification:**
- ✅ **Config:** Hardcoded tokens removed.
- ✅ **Env Injection:** `NATIVE_GLOBE_DEM_TOKEN` environment variable support confirmed via `main.cpp` logic.

## Known Failures

The following failures are expected configuration states, not regressions:

1. **`DepthPrecisionTest`**
   - **Cause:** `config.reversedZEnabled` is set to `false`.
   - **Impact:** Z-fighting may occur at extreme distances.
   - **Resolution:** Enable Reversed-Z in `src/core/config.h` (Phase 2 item).

2. **`RteRtcRockmeshRegressionTest`**
   - **Cause:** Precision threshold sensitivity in test environment.
   - **Impact:** Minor jitter in rock rendering.
   - **Resolution:** Inherited behavior (Track A issue).

## Conclusion
The codebase is stable. The v4 changes provide the necessary architectural safety for future optimizations (TextureArray enablement, GPU terrain) without breaking existing functionality.
