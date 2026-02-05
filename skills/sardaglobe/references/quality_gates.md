# Quality Gates

1. Parity check
   - Confirm behavior matches `globe-web-html/libs/webglobe.js` unless covered by the navigation exception.

2. LOD and SSE sanity
   - LOD selection should be monotonic with distance and stable under small camera changes.
   - Check SSE thresholds, tilt factor, and activation constants against JS.

3. Tile state machine
   - Validate state transitions and ensure no stuck states or redundant loads.

4. Async elevation
   - Ensure DEM requests are cached and do not block render.

5. Rendering stability
   - No visible seams, popping minimized by skirts or morphing where applicable.

6. Tests
   - Use `tests/lod_conformance_test.cpp` for numeric parity checks.
   - Use `tests/visual_lod_test.cpp` for visual regressions.

7. Documentation
   - If a phase is completed, update `docs/API_PORT_REVIEW_PROMPT.md` (phase log + snapshot).
