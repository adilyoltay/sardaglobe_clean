# Quality Gates

## Pre-Commit (Mandatory)
1. **CTest green** — `ctest --test-dir build --output-on-failure` must pass **35/35**.
2. **No new warnings** — Build with `-Wall` clean (stitch delta warnings are known/tracked).
3. **No illegal state transitions** — `TileStateMachine::unexpectedTransitions` stays 0.

## Parity Gates (Per-Feature)
4. **LOD conformance** — `LodConformanceTest` verifies neighbor LOD delta ≤ 1, monotonic refinement.
5. **Tile lifecycle** — `TileStateMachineCancelTest` + `TileSchedulerCancelFlowTest`: cancel→re-enter cycle clean, no retry counter inflation.
6. **Unpop/crossfade** — `UnpopCrossfadePolicyTest` + `TileFadeTest`: monotonic alpha, speed-bypass correct.
7. **Corner LODs** — `CornerLodTest`: edge mask → NW/NE/SE/SW weight mapping correct.
8. **Terrain morph** — `TileTerrainMorphTest`: 200ms blend, reset/restart behavior.
9. **Depth precision** — `DepthPrecisionTest`: log-depth and reversed-Z NDC mapping correct.
10. **DEM pipeline** — `DemContinuityTest` + `DemFallbackTest` + `DemCoEvictionTest`: ancestor fallback, edge delta, co-eviction.
11. **Cache layers** — `TileCacheStatsTest` + `MemoryTileCacheTest` + `DecodedTileBlobTest`: hit/miss/evict counters increment correctly.

## Visual Acceptance Gates (Pending Automation)
12. **Z-fighting** — No visible z-fighting at any standard camera position. **Status: manual check, CI gate pending.**
13. **Seam/crack** — No visible seams at LOD boundaries. **Status: manual check, CI gate pending.**
14. **Pop-free zoom** — Fast zoom in/out produces no visible tile pop. **Status: manual check, CI gate pending.**

## Performance Gates
15. **Frame time** — P95 < 16ms, P99 < 33ms at 1280×720 on mid-range hardware.
16. **No stuck tiles** — `staleTileCount` stays 0 after pipeline converges.
17. **Cancel effectiveness** — Viewport-out tiles transition to `Canceled` within `cancelAfterFramesUntouched` frames.

## Core Parity Acceptance (docs/CORE_GLOBE_PARITY_PLAN.md §5)
- All CTest green
- Hızlı pan/zoom: kritik pop = 0, kritik seam = 0, kritik z-fighting = 0
- Tile lifecycle: stuck state = 0, illegal transition = 0
- Terrain-aware interaction: orbit/pan/zoom target drift within threshold
- Frame metrics: P95/P99 regression gate passes
