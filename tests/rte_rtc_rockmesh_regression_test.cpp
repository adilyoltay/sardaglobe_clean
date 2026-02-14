// RTE/RTC RockMesh Regression Test
// Faz 1C: Validates RockMesh jitter-free rendering with RTE

#include <glm/glm.hpp>
#include <iostream>
#include <vector>
#include <cmath>

// Minimal standalone test - no GL dependencies

namespace {

bool Near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

void Report(const char* test) {
    std::cerr << "PASSED: " << test << '\n';
}

// WGS84 ellipsoid constants  
constexpr double WGS84_A_KM = 6378.137;

// Mock parsed node data
struct MockParsedData {
    std::string nodeKey;
    bool hasTransform = true;
    std::vector<float> transform;
    std::vector<int16_t> positions;
    std::vector<uint32_t> indices;
    int vertexCount = 0;
    int triangleCount = 0;
};

MockParsedData CreateMockData(const std::string& key, float tx, float ty, float tz) {
    MockParsedData parsed;
    parsed.nodeKey = key;
    parsed.hasTransform = true;
    
    // Column-major transform
    parsed.transform = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        tx, ty, tz, 1.0f
    };
    
    // Small mesh (1km scale in rock units)
    parsed.vertexCount = 4;
    float scale = 1000.0f / 6378.0f * 32768.0f;
    parsed.positions = {
        0, 0, 0,
        static_cast<int16_t>(scale), 0, 0,
        0, static_cast<int16_t>(scale), 0,
        0, 0, static_cast<int16_t>(scale)
    };
    
    parsed.indices = {0, 1, 2, 0, 2, 3};
    parsed.triangleCount = 2;
    
    return parsed;
}

// Simulate BuildMesh logic
void SimulateBuildMesh(const MockParsedData& parsed, 
                       std::vector<glm::dvec3>& outWorldPositions,
                       glm::dvec3& outOriginEcef) {
    glm::dmat4 M(1.0);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            M[col][row] = parsed.transform[col * 4 + row];
        }
    }
    
    glm::dvec3 t(M[3][0], M[3][1], M[3][2]);
    double kmPerRockUnit = WGS84_A_KM / glm::length(t);
    
    std::vector<glm::dvec3> worldPositions;
    glm::dvec3 bboxMin(1e18), bboxMax(-1e18);
    
    for (int i = 0; i < parsed.vertexCount; ++i) {
        double lx = parsed.positions[i * 3 + 0] / 32768.0;
        double ly = parsed.positions[i * 3 + 1] / 32768.0;
        double lz = parsed.positions[i * 3 + 2] / 32768.0;
        
        glm::dvec4 local(lx, ly, lz, 1.0);
        glm::dvec3 world = glm::dvec3(M * local) * kmPerRockUnit;
        worldPositions.push_back(world);
        bboxMin = glm::min(bboxMin, world);
        bboxMax = glm::max(bboxMax, world);
    }
    
    outOriginEcef = (bboxMin + bboxMax) * 0.5;
    outWorldPositions = worldPositions;
}

} // namespace

int main() {
    int failures = 0;
    
    // Test 1: Build mesh computes origin near Earth radius
    {
        auto parsed = CreateMockData("test_node_1", 6378.0f, 0.0f, 0.0f);
        
        std::vector<glm::dvec3> worldPositions;
        glm::dvec3 originEcef;
        SimulateBuildMesh(parsed, worldPositions, originEcef);
        
        float originLen = glm::length(glm::vec3(originEcef));
        if (!Expect(Near(originLen, 6378.0f, 10.0f), "Origin should be near Earth radius")) failures++;
        
        // Split origin
        glm::vec3 originHi = glm::vec3(originEcef);
        glm::vec3 originLo = glm::vec3(originEcef - glm::dvec3(originHi));
        
        float hiLen = glm::length(originHi);
        if (!Expect(hiLen > 6000.0f, "Origin should contain major magnitude")) failures++;
        
        if (Near(originLen, 6378.0f, 10.0f) && hiLen > 6000.0f) {
            Report("BuildMeshComputesOrigin");
        }
    }
    
    // Test 2: Vertices relative to origin
    {
        auto parsed = CreateMockData("test_node_2", 6378.0f, 100.0f, 50.0f);
        
        std::vector<glm::dvec3> worldPositions;
        glm::dvec3 originEcef;
        SimulateBuildMesh(parsed, worldPositions, originEcef);
        
        bool allRelative = true;
        bool allReconstruct = true;
        
        for (const auto& world : worldPositions) {
            glm::vec3 relative = glm::vec3(world - originEcef);
            float relLen = glm::length(relative);
            
            if (relLen >= 2.0f) {
                allRelative = false;
                break;
            }
            
            glm::vec3 reconstructed = glm::vec3(originEcef) + relative;
            float error = glm::length(reconstructed - glm::vec3(world));
            
            if (error >= 1e-4f) {
                allReconstruct = false;
                break;
            }
        }
        
        if (!Expect(allRelative, "RockMesh vertices should be relative to origin")) failures++;
        if (!Expect(allReconstruct, "Reconstruction should be accurate")) failures++;
        
        if (allRelative && allReconstruct) Report("VerticesRelativeToOrigin");
    }
    
    // Test 3: Origin preserves precision (split test)
    {
        // Create mesh at precise location
        auto parsed = CreateMockData("test_node_3", 6378.137f, 100.456f, 50.789f);
        
        std::vector<glm::dvec3> worldPositions;
        glm::dvec3 originEcef;
        SimulateBuildMesh(parsed, worldPositions, originEcef);
        
        // Split and reconstruct
        glm::vec3 originHi = glm::vec3(originEcef);
        glm::vec3 originLo = glm::vec3(originEcef - glm::dvec3(originHi));
        glm::vec3 reconstructed = originHi + originLo;
        
        float error = glm::length(reconstructed - glm::vec3(originEcef));
        if (!Expect(error < 1e-4f, "Origin split should preserve precision")) failures++;
        else Report("OriginPreservesPrecision");
    }
    
    // Test 4: Multiple meshes have different origins
    {
        auto parsed1 = CreateMockData("node_a", 6378.0f, 0.0f, 0.0f);
        auto parsed2 = CreateMockData("node_b", 6378.0f, 100.0f, 0.0f);  // 100km offset
        
        std::vector<glm::dvec3> worldPos1, worldPos2;
        glm::dvec3 origin1, origin2;
        
        SimulateBuildMesh(parsed1, worldPos1, origin1);
        SimulateBuildMesh(parsed2, worldPos2, origin2);
        
        if (!Expect(origin1 != origin2, "Different meshes should have different origins")) failures++;
        
        float dist = glm::distance(glm::vec3(origin1), glm::vec3(origin2));
        if (!Expect(dist > 10.0f && dist < 500.0f, "Mesh origins should be reasonably separated")) failures++;
        
        if (origin1 != origin2 && dist > 10.0f && dist < 500.0f) {
            Report("MultipleMeshesHaveDifferentOrigins");
        }
    }
    
    // Test 5: RTE consistency check
    {
        // Validate that vertex positions after relative encoding are small
        auto parsed = CreateMockData("test_large", 6378.0f, 500.0f, 500.0f);
        
        std::vector<glm::dvec3> worldPositions;
        glm::dvec3 originEcef;
        SimulateBuildMesh(parsed, worldPositions, originEcef);
        
        // Compute relative positions
        float maxRelLen = 0.0f;
        float minRelLen = 1e18f;
        
        for (const auto& world : worldPositions) {
            glm::vec3 relative = glm::vec3(world - originEcef);
            float len = glm::length(relative);
            maxRelLen = std::max(maxRelLen, len);
            minRelLen = std::min(minRelLen, len);
        }
        
        // Max relative should be small (mesh size ~1km)
        if (!Expect(maxRelLen < 2.0f, "Max relative position should be small")) failures++;
        
        // Range should be reasonable
        float range = maxRelLen - minRelLen;
        if (!Expect(range > 0.0f && range < 2.0f, "Relative position range should be reasonable")) failures++;
        
        if (maxRelLen < 2.0f && range > 0.0f && range < 2.0f) {
            Report("RTEConsistencyCheck");
        }
    }
    
    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }
    
    std::cerr << "\nAll RTE RockMesh tests PASSED\n";
    return 0;
}
