# Faz 1 - Reversed-Z ve RTE/RTC Implementasyon Kılavuzu

> **Hedef:** Titreme ve z-fighting'i ortadan kaldırmak  
> **Süre:** 3-4 gün  
> **Öncelik:** P0 (Engelleyici)

---

## Bölüm 1: Reversed-Z Derinlik Stabilitesi

### 1.1 Teori

**Standart Z (glDepthFunc(GL_LEQUAL)):**
- Near plane: Z = -1, Far plane: Z = 1
- Float24/float32: Değerler near'da yoğun, far'da seyrek
- Problem: Dünya ölçeğinde (km) uzak objeler z-fighting yapar

**Reversed-Z (glDepthFunc(GL_GEQUAL)):**
- Near plane: Z = 1, Far plane: Z = 0 (ters çevrilmiş)
- Float değerleri far'da yoğun, near'da seyrek
- Avantaj: Uzak objeler daha yüksek hassasiyet alır (gereken yer)

**GL State Değişimi:**
```cpp
// Eski
void Clear() {
    glClearDepth(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void SetupDepth() {
    glDepthFunc(GL_LEQUAL);
}

// Yeni (Reversed-Z)
void Clear() {
    glClearDepth(0.0f);  // Far = 0
    glClear(GL_DEPTH_BUFFER_BIT);
}

void SetupDepth() {
    glDepthFunc(GL_GEQUAL);  // Daha büyük Z = daha yakın
}
```

### 1.2 Projection Matrisi

```cpp
// src/camera/earth_camera.cpp
// Mevcut implementasyon zaten doğru (GetProjectionMatrix içinde)
// Sadece doğrulama:

void PerspectiveCamera::UpdateMatrices() const {
    if (m_proj_dirty) {
        if (!m_reverseZ) {
            // Standart
            m_proj_matrix = glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
        } else {
            // Reversed-Z projection
            const double f = 1.0 / std::tan(glm::radians(m_fov) * 0.5);
            const double n = m_near;
            const double fa = m_far;

            m_proj_matrix = glm::dmat4(0.0);
            m_proj_matrix[0][0] = f / m_aspect;
            m_proj_matrix[1][1] = f;
            // Reversed-Z: near -> +1, far -> -1
            m_proj_matrix[2][2] = (n + fa) / (fa - n);  // Bu aslında standart
            // Ama clip space'i ters çevirmek için:
            m_proj_matrix[2][2] = -(fa + n) / (fa - n);  // Clip Z negatif olur
            m_proj_matrix[2][3] = -1.0;
            m_proj_matrix[3][2] = -(2.0 * fa * n) / (fa - n);
        }
        m_proj_dirty = false;
    }
}
```

**Doğru Reversed-Z Projection:**
```cpp
// OpenGL NDC [-1, 1] için doğru formül
// gl_Position.z = -1 (far), +1 (near)
// Ama gl_FragCoord.z [0, 1] için ters çevir

// Doğru yaklaşım:
// Clip space Z: -1 (far), +1 (near)
// gl_FragCoord.z: 0 (far), 1 (near) -> glDepthFunc(GL_GEQUAL)

void PerspectiveCamera::UpdateMatricesReversedZ() const {
    const double f = 1.0 / std::tan(glm::radians(m_fov) * 0.5);
    const double n = m_near;
    const double fa = m_far;

    m_proj_matrix = glm::dmat4(0.0);
    m_proj_matrix[0][0] = f / m_aspect;
    m_proj_matrix[1][1] = f;
    // Reversed: -1 at near, 1 at far (OpenGL convention'a göre)
    // Aslında: glClipControl ile daha temiz yapılabilir
    // GL 4.5+: glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)
    
    // GL 3.3 ile çözüm:
    // Standart projection ama gl_Position.z = -gl_Position.z
    m_proj_matrix[2][2] = (fa + n) / (n - fa);  // Standart ama işaret değişimi
    m_proj_matrix[2][3] = -1.0;
    m_proj_matrix[3][2] = (2.0 * fa * n) / (n - fa);
}
```

**Shader Tarafı (Vertex):**
```glsl
#version 330 core

uniform mat4 uMVP;
uniform bool uReversedZ;

in vec3 aPosition;

void main() {
    vec4 clipPos = uMVP * vec4(aPosition, 1.0);
    
    if (uReversedZ) {
        // gl_FragCoord.z = 0 (far), 1 (near)
        // Clip space Z = -1 (far), +1 (near)
        // Reversed: gl_Position.z *= -1 değil,
        // Doğrusu: clip space'i [0,1] yap
        
        // GL 3.3 Reversed-Z için:
        // clipPos.z = clipPos.w - clipPos.z;  // Bu yanlış
        
        // Doğru formül:
        // gl_Position.z = clipPos.z;
        // gl_Position.w = clipPos.w;
        // GL depth test GEQUAL ile 0 (far) < 1 (near)
        
        // Ama bu işe yaramaz çünkü gl_FragCoord.z = gl_Position.z / gl_Position.w
        
        // Çözüm: gl_Position.z = -gl_Position.z (NDC flip)
        // VEYA: infinite far ile zero-to-one clip space
    }
    
    gl_Position = clipPos;
}
```

**Önerilen Çözüm (Infinite Far):**
```glsl
// Vertex shader
void main() {
    vec4 viewPos = uViewMatrix * vec4(worldPos, 1.0);
    
    // Infinite far, reversed-Z
    // Near = 1m, Far = infinity
    float f = 1.0 / tan(uFov * 0.5);
    float e = 1e-6;  // epsilon
    
    mat4 proj = mat4(
        f / uAspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, e, 1,      // Z component: near = 1, far = 0
        0, 0, e * uNear, 0  // W component
    );
    
    gl_Position = proj * viewPos;
}

// GL setup
glDepthFunc(GL_GEQUAL);
glClearDepth(0.0f);
```

### 1.3 Implementasyon Adımları

**Adım 1: Camera düzeltmesi**
```cpp
// src/camera/earth_camera.cpp
void PerspectiveCamera::UpdateMatrices() const {
    if (m_view_dirty) {
        glm::dvec3 f, u, r;
        GetBasisVectors(f, u, r);
        m_view_matrix = glm::lookAt(m_pos_ecef, m_pos_ecef + f, u);
        m_view_dirty = false;
    }
    
    if (m_proj_dirty) {
        if (!m_reverseZ) {
            m_proj_matrix = glm::perspective(
                glm::radians(m_fov), m_aspect, m_near, m_far);
        } else {
            // Reversed-Z with infinite far plane
            const double f = 1.0 / std::tan(glm::radians(m_fov) * 0.5);
            const double n = m_near;
            const double e = 1e-6;  // Very small epsilon
            
            m_proj_matrix = glm::dmat4(0.0);
            m_proj_matrix[0][0] = f / m_aspect;
            m_proj_matrix[1][1] = f;
            m_proj_matrix[2][2] = e;     // Near = 1, Far = 0
            m_proj_matrix[2][3] = 1.0;   // Perspective divide
            m_proj_matrix[3][2] = e * n; // Near plane offset
        }
        m_proj_dirty = false;
    }
}
```

**Adım 2: GL State setup**
```cpp
// src/engine/globe_engine.cpp
void GlobeEngine::Init() {
    // ... mevcut init kodu ...
    
    // Depth test setup
    glEnable(GL_DEPTH_TEST);
    if (config_.reversedZEnabled) {
        glDepthFunc(GL_GEQUAL);
        glClearDepth(0.0f);  // Clear to far
    } else {
        glDepthFunc(GL_LEQUAL);
        glClearDepth(1.0f);
    }
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

void GlobeEngine::Render() {
    // Clear
    if (config_.reversedZEnabled) {
        glClearDepth(0.0f);
    } else {
        glClearDepth(1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // ... render code ...
}
```

---

## Bölüm 2: RTE/RTC (Relative to Eye/Center)

### 2.1 Problem: Jitter/Precision

**Mevcut:**
- Tile vertex'leri ECEF dünya koordinatlarında (~6378km)
- 32-bit float: ~7 basamak hassasiyet
- 6378137.0f + 0.001f = 6378137.0f (kaybolan hassasiyet!)
- Yakın zoom'da (kamera yere yakın) titreme görülür

**Çözüm - RTE:**
- Vertex pozisyonları = WorldPos - CameraPos (relative)
- Sonuç: ~km seviyesinde, float32 yeterli
- View matrix = identity (veya optimize edilmiş)

**Çözüm - RTC:**
- Tile'ın kendi origin'ine göre relative pozisyonlar
- Camera ile tile origin arası double-precision hesaplanır
- Vertex offset'leri float32 ile gönderilir

### 2.2 RTC Implementasyonu

**Vertex Format:**
```cpp
// Mevcut (src/core/tile.h içinde Tile struct)
// Mesh vertex'leri GPU buffer'da

// Yeni: Tile origin ekle
struct Tile {
    // ... mevcut alanlar ...
    
    // RTC origin (double precision split)
    glm::vec3 originEcefHi;  // High 16 bits
    glm::vec3 originEcefLo;  // Low 16 bits
};
```

**Mesh Builder Değişikliği:**
```cpp
// src/rendering/tile_mesh_builder.cpp
void TileMeshBuilder::Build(Tile& tile, ...) {
    // Tile'ın dünya merkezindeki pozisyonu
    glm::dvec3 tileCenterECEF = TileCenterWorld(tile.key);
    
    // Split to high/low for double emulation
    // 1.0 = 1.0 + 0.0
    // 1234567.89 = 1234000.0 + 567.89
    tile.originEcefHi = glm::vec3(tileCenterECEF);
    tile.originEcefLo = glm::vec3(tileCenterECEF - glm::dvec3(tile.originEcefHi));
    
    // Vertex'leri tile-center'a göre relative hesapla
    for (int y = 0; y <= segments; ++y) {
        for (int x = 0; x <= segments; ++x) {
            // Lat/lon/alt hesapla
            glm::dvec3 worldPos = LatLonAltToECEF(lat, lon, alt);
            
            // Relative to tile center
            glm::vec3 relativePos = glm::vec3(worldPos - tileCenterECEF);
            
            // Store relative position
            vertices.push_back(relativePos.x);
            vertices.push_back(relativePos.y);
            vertices.push_back(relativePos.z);
        }
    }
}
```

**Shader:**
```glsl
// tile_vertex.glsl
uniform vec3 uTileOriginECEFHi;
uniform vec3 uTileOriginECEFLo;
uniform vec3 uCameraECEFHi;
uniform vec3 uCameraECEFLo;
uniform mat4 uProjectionMatrix;

in vec3 aRelativePosition;  // Tile origin'a göre relative

vec3 AddDouble(vec3 hi, vec3 lo) {
    return hi + lo;
}

void main() {
    // Double-precision tile origin
    vec3 tileOrigin = uTileOriginECEFHi + uTileOriginECEFLo;
    
    // World position = tileOrigin + relativePosition
    vec3 worldPos = tileOrigin + aRelativePosition;
    
    // Double-precision camera position
    vec3 cameraPos = uCameraECEFHi + uCameraECEFLo;
    
    // View-relative position (now small enough for float32)
    vec3 viewRelative = worldPos - cameraPos;
    
    // Apply view+projection
    // View matrix = identity çünkü zaten camera-relative
    gl_Position = uProjectionMatrix * vec4(viewRelative, 1.0);
}
```

**CPU Tarafında View Matrix:**
```cpp
// Eski: view matrix ile transform
// glm::mat4 mvp = projection * view * model;

// Yeni: view transform zaten yapıldı (relative positions)
// Sadece projection gerekli
glm::mat4 proj = camera_->GetProjectionMatrix();
// View matrix = identity (veya sadece rotation)
```

### 2.3 RockMesh için RTE

**RockMesh vertex'leri:**
```cpp
// src/io/providers/rocktree_node_data_parser.cpp
// ParsedNodeData içinde vertex'ler absolute ECEF olarak geliyor

// RTC uygula
void BuildRtcMesh(const ParsedNodeData& parsed, RockMeshCpu& outMesh) {
    // Mesh'in bounding box'ından origin seç
    glm::dvec3 meshOrigin = (parsed.bboxMin + parsed.bboxMax) * 0.5;
    
    // Split origin
    glm::vec3 originHi = glm::vec3(meshOrigin);
    glm::vec3 originLo = glm::vec3(meshOrigin - glm::dvec3(originHi));
    
    // Store origin in mesh metadata
    outMesh.originEcefHi = originHi;
    outMesh.originEcefLo = originLo;
    
    // Vertex'leri relative olarak encode et
    outMesh.vertices.reserve(parsed.vertices.size() * 9);  // stride
    for (const auto& v : parsed.vertices) {
        glm::dvec3 absPos = LatLonAltToECEF(v.lat, v.lon, v.alt);
        glm::vec3 relPos = glm::vec3(absPos - meshOrigin);
        
        // pos(3), normal(3), uv(2), padding(1)
        outMesh.vertices.push_back(relPos.x);
        outMesh.vertices.push_back(relPos.y);
        outMesh.vertices.push_back(relPos.z);
        // ... normal, uv ...
    }
}
```

---

## Bölüm 3: Test Planı

### 3.1 Reversed-Z Testi

```cpp
// tests/reversed_z_precision_test.cpp
#include <gtest/gtest.h>
#include "../src/camera/earth_camera.h"
#include <glm/gtc/epsilon.hpp>

using namespace earth;

TEST(ReversedZ, ProjectionMatrix) {
    PerspectiveCamera cam;
    cam.SetReverseZEnabled(true);
    cam.SetNearFar(1.0, 1000000.0);  // 1m to 1000km
    cam.SetFov(45.0);
    cam.SetAspectRatio(16.0 / 9.0);
    
    glm::dmat4 proj = cam.GetProjectionMatrix();
    
    // Test near plane maps to Z = 1 (NDC)
    glm::dvec4 nearPoint(0, 0, -1.0, 1);  // View space near
    glm::dvec4 nearClip = proj * nearPoint;
    nearClip /= nearClip.w;
    EXPECT_NEAR(nearClip.z, 1.0, 1e-6);
    
    // Test far plane maps to Z = 0 (NDC)
    glm::dvec4 farPoint(0, 0, -1000000.0, 1);
    glm::dvec4 farClip = proj * farPoint;
    farClip /= farClip.w;
    EXPECT_NEAR(farClip.z, 0.0, 1e-2);  // Looser tolerance for far
}

TEST(ReversedZ, DepthPrecisionComparison) {
    // Far-away objects should have better precision with reversed-Z
    const double near = 1.0;
    const double far = 1000000.0;
    
    // Standard Z at far
    double z_standard = far / (far - near);
    
    // Reversed Z at far
    double z_reversed = near / (far - near);  // Small value
    
    // Reversed Z should have more float32 mantissa bits at far distances
    // (This is a conceptual test - actual precision measured via render)
    EXPECT_LT(z_reversed, z_standard);
}
```

### 3.2 RTE/RTC Testi

```cpp
// tests/rte_rtc_regression_test.cpp
TEST(RteRtc, MicroMovementVertexJitter) {
    // Setup camera at a specific position
    PerspectiveCamera cam;
    cam.SetLatLonAlt(0, 0, 10000);  // 10km altitude
    
    // Tile at equator
    TileKey key{5, 16, 16};  // Some tile near equator
    glm::dvec3 tileCenter = TileCenterWorld(key);
    
    // Simulate small camera movements
    std::vector<double> errors;
    for (int i = 0; i < 100; ++i) {
        double offset = i * 0.0001;  // 0.1m increments
        cam.SetLatLonAlt(0, offset / 111000.0, 10000);
        
        // Get view projection
        glm::dmat4 vp = cam.GetViewProjectionMatrix();
        
        // Project tile center
        glm::dvec4 clip = vp * glm::dvec4(tileCenter, 1.0);
        clip /= clip.w;
        
        // Screen position
        double screenX = (clip.x + 1) * 0.5 * 1920;
        
        // Error from previous frame
        if (i > 0) {
            double expectedDelta = offset * 0.001;  // Approximate
            errors.push_back(std::abs(screenX - expectedDelta));
        }
    }
    
    // Average error should be very small with RTE
    double avgError = std::accumulate(errors.begin(), errors.end(), 0.0) / errors.size();
    EXPECT_LT(avgError, 0.5);  // Less than 0.5 pixel average jitter
}
```

---

## Bölüm 4: Checklist

### Reversed-Z Implementasyon Checklist

- [ ] `PerspectiveCamera::UpdateMatrices()` Reversed-Z projeksiyon doğru
- [ ] `GlobeEngine::Init()` GL state `GL_GEQUAL` + `glClearDepth(0)`
- [ ] `GlobeEngine::Render()` clear depth değeri Reversed-Z'ye göre
- [ ] Shader'lar Reversed-Z ile uyumlu (test edilmeli)
- [ ] Depth buffer precision test geçiyor

### RTE/RTC Implementasyon Checklist

- [ ] Tile struct'a `originEcefHi/Lo` eklendi
- [ ] `TileMeshBuilder::Build()` relative vertex hesaplıyor
- [ ] Vertex shader double-emulation matematiği doğru
- [ ] Uniform binding düzgün çalışıyor
- [ ] RockMesh için RTC origin seçimi implemente edildi
- [ ] Jitter test geçiyor (< 0.5px RMS)

### Entegrasyon Checklist

- [ ] Feature flag'ler çalışıyor (`useReversedZ`, `useRteRender`)
- [ ] Eski yol (non-RTE) hala çalışıyor (backward compat)
- [ ] Debug panel'de durum gösteriliyor
- [ ] Tüm mevcut testler geçiyor
- [ ] Yeni Reversed-Z + RTE testleri geçiyor
