# P3 Gate Backlog - Advanced GE Parity Features

> **Status:** Backlogged (Not Required for P1/P2 Gates)  
> **Priority:** Low  
> **Estimated Effort:** 5-7 days  

---

## 1. Per-Tile Depth Planes

### Current State
- **Implemented:** Log-depth (`config.logDepthEnabled = true`)
- **Implemented:** Reversed-Z (`config.reversedZEnabled = false`)
- **Missing:** Per-tile depth plane equations (GE feature)

### GE Reference
```
WASM strings found:
- "Plane equations for computing depth of each tile mesh vertex"
- "uDepthPlane" (uniform)
- Per-tile linearized depth for extreme zoom
```

### Implementation Path
```cpp
// 1. Add to tile_mesh_builder.cpp
struct DepthPlane {
    glm::vec4 coefficients;  // ax + by + cz + d = 0
};

DepthPlane ComputeTileDepthPlane(
    const TileKey& key,
    const glm::vec3& cameraPos,
    const Extent& extent
) {
    // Fit plane through tile corners, perpendicular to view
    // Returns vec4(a, b, c, d) for plane equation
}

// 2. Add to shader_manager.cpp vertex shader
uniform vec4 uDepthPlane;
uniform int uUseDepthPlane;  // 0=log-depth, 1=per-tile

// In vertex shader:
float tileDepth = dot(worldPos, uDepthPlane.xyz) + uDepthPlane.w;
gl_Position.z = mix(logDepth, tileDepth, float(uUseDepthPlane));
```

### Acceptance Criteria
- [ ] Zero z-fighting at zoom 19-20
- [ ] Unit tests for plane computation
- [ ] <5% performance overhead
- [ ] Optional (disabled by default until stable)

---

## 2. RPC DEM Path (BatchGetElevationsByPoint)

### Current State
- **Implemented:** Terrain-RGB (MapTiler/Mapbox)
- **Implemented:** Pirireis batch (bbox-based)
- **Missing:** Google Earth-style protobuf RPC

### GE Reference
```
WASM strings found:
- "google.internal.earth.v1.terrain.BatchGetElevationsByPointRequest"
- "RefinedElevationsRequester"
- "GetAccurateTerrainElevation"
```

### Implementation Path

#### 2.1 Protobuf Schema (Required)
```protobuf
// proto/earth_terrain.proto
syntax = "proto3";
package google.internal.earth.v1.terrain;

message BatchGetElevationsByPointRequest {
    repeated LatLonPoint points = 1;
    ElevationType elevation_type = 2;
}

message LatLonPoint {
    double latitude = 1;
    double longitude = 2;
}

message BatchGetElevationsByPointResponse {
    repeated double elevations = 1;
}

enum ElevationType {
    ELLIPSOID = 0;
    TERRAIN = 1;
    SEA_LEVEL = 2;
}
```

#### 2.2 DEM Source Type Extension
```cpp
// config.h
enum class DemSourceType {
    Auto,
    PirireisBatch,
    TerrainRGBMapbox,
    TerrainRGBTerrarium,
    GoogleEarthRPC  // NEW
};
```

#### 2.3 RPC Implementation
```cpp
// io/dem_rpc_client.h
class DemRpcClient {
public:
    struct Config {
        std::string endpoint;  // "https://earth.googleapis.com/v1/terrain"
        std::string apiKey;
        std::string oauthToken;
    };
    
    // Batch elevation query
    std::future<std::vector<double>> QueryElevations(
        const std::vector<LatLonPoint>& points,
        ElevationType type
    );
};
```

### Prerequisites
- [ ] Google Earth API access (requires Google partnership/enterprise agreement)
- [ ] OAuth2 authentication flow
- [ ] Protobuf + gRPC dependency

### Acceptance Criteria
- [ ] Sub-meter elevation precision
- [ ] Batch queries (up to 1000 points)
- [ ] Async/await pattern
- [ ] Fallback to Terrain-RGB on RPC failure

---

## 3. Implementation Order

### Phase P3.1: Depth Planes (3-4 days)
1. Plane equation computation
2. Shader integration
3. Unit tests
4. Performance validation

### Phase P3.2: RPC Client (4-5 days)
1. Protobuf definitions
2. gRPC client implementation
3. OAuth integration
4. Error handling & fallback

---

## 4. Risk Assessment

| Feature | Risk | Mitigation |
|---------|------|------------|
| Depth Planes | High complexity | Make optional, extensive testing |
| RPC DEM | API access required | Partner with Google, have fallback |
| Performance | Unknown overhead | Benchmark before enabling |

---

## 5. Notes

- P3 features are **NOT blockers** for P1/P2 gates
- Current implementation (log-depth + Terrain-RGB) is sufficient for 95% use cases
- Implement P3 only if:
  - Z-fighting issues reported at extreme zoom
  - Sub-meter elevation precision required
  - Google Earth API access secured

---

**Last Updated:** 2026-02-13  
**Owner:** SardaGlobe Team  
**Review Date:** After P2 gate completion
