// GE NodeData Parser Test
// Tests RockTreeNodeDataParser with correct GE encoding format
// Reference: retroplasma/earth-reverse-engineering (rocktree_decoder.h)

#include "../src/io/providers/rocktree_node_data_parser.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <vector>

using namespace globe;

// Helper: Encode varint to buffer, returns bytes written
size_t EncodeVarint(uint8_t* out, uint64_t value) {
    size_t pos = 0;
    while (value >= 0x80) {
        out[pos++] = static_cast<uint8_t>(value | 0x80);
        value >>= 7;
    }
    out[pos++] = static_cast<uint8_t>(value);
    return pos;
}

// Helper: Append varint to vector
void AppendVarint(std::vector<uint8_t>& buf, uint64_t value) {
    uint8_t tmp[10];
    size_t n = EncodeVarint(tmp, value);
    buf.insert(buf.end(), tmp, tmp + n);
}

// Helper: Create field key byte (for small field numbers)
uint8_t MakeFieldKey(uint32_t fieldNum, uint8_t wireType) {
    return static_cast<uint8_t>((fieldNum << 3) | wireType);
}

// Helper: Append field key + length-delimited payload
void AppendField(std::vector<uint8_t>& msg, uint32_t fieldNum, const std::vector<uint8_t>& payload) {
    AppendVarint(msg, (fieldNum << 3) | 2);  // length-delimited wire type
    AppendVarint(msg, payload.size());
    msg.insert(msg.end(), payload.begin(), payload.end());
}

// Helper: Append field key + raw bytes
void AppendFieldRaw(std::vector<uint8_t>& msg, uint32_t fieldNum, const uint8_t* data, size_t len) {
    AppendVarint(msg, (fieldNum << 3) | 2);
    AppendVarint(msg, len);
    msg.insert(msg.end(), data, data + len);
}

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

// Build delta-encoded vertex data (GE format: planar [X deltas][Y deltas][Z deltas])
std::vector<uint8_t> BuildVertexData(const std::vector<uint8_t>& positions) {
    // positions: interleaved x,y,z uint8 values
    size_t count = positions.size() / 3;
    std::vector<uint8_t> planar(count * 3);
    
    uint8_t prevX = 0, prevY = 0, prevZ = 0;
    for (size_t i = 0; i < count; i++) {
        uint8_t x = positions[i * 3 + 0];
        uint8_t y = positions[i * 3 + 1];
        uint8_t z = positions[i * 3 + 2];
        planar[count * 0 + i] = static_cast<uint8_t>(x - prevX);  // delta X
        planar[count * 1 + i] = static_cast<uint8_t>(y - prevY);  // delta Y
        planar[count * 2 + i] = static_cast<uint8_t>(z - prevZ);  // delta Z
        prevX = x; prevY = y; prevZ = z;
    }
    return planar;
}

// Build GE index data (zeros-val algorithm, varint encoded)
std::vector<uint8_t> BuildIndexData(const std::vector<uint16_t>& strip) {
    std::vector<uint8_t> buf;
    AppendVarint(buf, strip.size());  // strip length
    
    // Reverse the zeros-val algorithm:
    // Forward: c = zeros - val; if val==0 then zeros++
    // We need to find val given the strip indices
    int zeros = 0;
    int a = 0, b = 0, c = 0;
    for (size_t i = 0; i < strip.size(); i++) {
        a = b; b = c;
        c = strip[i];
        int val = zeros - c;
        AppendVarint(buf, val);
        if (val == 0) zeros++;
    }
    return buf;
}

// Build minimal valid NodeData with correct GE encoding
std::vector<uint8_t> BuildMinimalNodeData() {
    // 3 vertices: (0,0,0), (100,0,0), (0,100,0)
    std::vector<uint8_t> positions = {0,0,0, 100,0,0, 0,100,0};
    std::vector<uint8_t> vertexData = BuildVertexData(positions);
    
    // Triangle strip: [0, 1, 2] → 1 non-degenerate triangle
    std::vector<uint16_t> strip = {0, 1, 2};
    std::vector<uint8_t> indexData = BuildIndexData(strip);
    
    // Build mesh sub-message
    std::vector<uint8_t> mesh;
    AppendFieldRaw(mesh, 1, vertexData.data(), vertexData.size());  // field 1: vertices
    AppendFieldRaw(mesh, 3, indexData.data(), indexData.size());     // field 3: indices
    
    // Build top-level NodeData
    std::vector<uint8_t> topLevel;
    
    // field 1: transform (identity-like, with translation for valid tLen)
    double tfm[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 6371000,0,0,1};
    AppendFieldRaw(topLevel, 1, reinterpret_cast<const uint8_t*>(tfm), 128);
    
    // field 2: mesh
    AppendField(topLevel, 2, mesh);
    
    return topLevel;
}

// Build full NodeData with texture
std::vector<uint8_t> BuildFullNodeData() {
    // 4 vertices: (0,0,0), (255,0,0), (255,255,0), (0,255,0)
    std::vector<uint8_t> positions = {0,0,0, 255,0,0, 255,255,0, 0,255,0};
    std::vector<uint8_t> vertexData = BuildVertexData(positions);
    
    // Triangle strip: [0, 1, 2, 3] → 2 non-degenerate triangles
    std::vector<uint16_t> strip = {0, 1, 2, 3};
    std::vector<uint8_t> indexData = BuildIndexData(strip);
    
    // Build texture sub-message
    std::vector<uint8_t> texMsg;
    uint8_t fakeJpeg[] = {0xFF, 0xD8, 0xFF, 0xE0};
    AppendFieldRaw(texMsg, 1, fakeJpeg, 4);     // field 1: JPEG bytes
    texMsg.push_back(MakeFieldKey(3, 0));        // field 3: width (varint)
    AppendVarint(texMsg, 256);
    texMsg.push_back(MakeFieldKey(4, 0));        // field 4: height (varint)
    AppendVarint(texMsg, 256);
    
    // UV offset and scale (field 10: 4 floats)
    float uvQuant[] = {0.0f, 0.0f, 1.0f / 256.0f, 1.0f / 256.0f};
    
    // Build mesh sub-message
    std::vector<uint8_t> mesh;
    AppendFieldRaw(mesh, 1, vertexData.data(), vertexData.size());
    AppendFieldRaw(mesh, 3, indexData.data(), indexData.size());
    AppendField(mesh, 6, texMsg);
    AppendFieldRaw(mesh, 10, reinterpret_cast<const uint8_t*>(uvQuant), 16);
    
    // Build top-level
    std::vector<uint8_t> topLevel;
    double tfm[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 100,200,300,1};
    AppendFieldRaw(topLevel, 1, reinterpret_cast<const uint8_t*>(tfm), 128);
    AppendField(topLevel, 2, mesh);
    
    return topLevel;
}

int main() {
    int failed = 0;
    std::cout << "=== GE NodeData Parser Test ===\n";

    // Test 1: UnpackVertices - delta decode correctness
    {
        // Input: 3 vertices in planar delta format
        // Vertex 0: (10, 20, 30)  → deltas: 10, 20, 30
        // Vertex 1: (50, 20, 30)  → deltas: 40, 0, 0
        // Vertex 2: (50, 80, 30)  → deltas: 0, 60, 0
        uint8_t planar[] = {10, 40, 0,   // X deltas
                            20, 0, 60,   // Y deltas
                            30, 0, 0};   // Z deltas
        auto result = RockTreeNodeDataParser::UnpackVertices(planar, 9);
        
        failed += !Expect(result.size() == 9, "UnpackVertices should produce 9 bytes");
        failed += !Expect(result[0] == 10 && result[1] == 20 && result[2] == 30, "V0 should be (10,20,30)");
        failed += !Expect(result[3] == 50 && result[4] == 20 && result[5] == 30, "V1 should be (50,20,30)");
        failed += !Expect(result[6] == 50 && result[7] == 80 && result[8] == 30, "V2 should be (50,80,30)");
        std::cout << "  UnpackVertices delta decode: OK\n";
    }
    
    // Test 2: UnpackVertices - uint8 wrapping
    {
        // Test wrapping: 200 + 100 = 44 (mod 256)
        uint8_t planar[] = {200, 100,  0, 0,  0, 0};
        auto result = RockTreeNodeDataParser::UnpackVertices(planar, 6);
        failed += !Expect(result[0] == 200 && result[3] == 44, "uint8 wrapping should work");
        std::cout << "  UnpackVertices wrapping: OK\n";
    }

    // Test 3: UnpackIndices - zeros-val algorithm
    {
        // Strip: [0, 1, 2] → 3 values
        // zeros-val reverse: val0=0-0=0 (zeros=1), val1=1-1=0 (zeros=2), val2=2-2=0 (zeros=3)
        std::vector<uint16_t> strip = {0, 1, 2};
        auto indexData = BuildIndexData(strip);
        auto result = RockTreeNodeDataParser::UnpackIndices(indexData.data(), indexData.size());
        
        failed += !Expect(result.size() == 3, "Should have 3 strip indices");
        failed += !Expect(result[0] == 0 && result[1] == 1 && result[2] == 2,
                         "Strip should be [0,1,2]");
        std::cout << "  UnpackIndices basic: OK\n";
    }
    
    // Test 4: StripToTriangleList
    {
        std::vector<uint16_t> strip = {0, 1, 2, 3};
        std::vector<uint32_t> triangles;
        bool ok = RockTreeNodeDataParser::StripToTriangleList(strip, triangles);
        
        failed += !Expect(ok, "Strip conversion should succeed");
        failed += !Expect(triangles.size() == 6, "Should produce 2 triangles (6 indices)");
        
        // Triangle 0 (i=2, even): (0,1,2)
        failed += !Expect(triangles[0] == 0 && triangles[1] == 1 && triangles[2] == 2,
                         "First triangle should be (0,1,2)");
        // Triangle 1 (i=3, odd): (1,3,2) — winding flip
        failed += !Expect(triangles[3] == 1 && triangles[4] == 3 && triangles[5] == 2,
                         "Second triangle should be (1,3,2)");
        std::cout << "  StripToTriangleList: OK\n";
    }
    
    // Test 5: Degenerate triangle skipping
    {
        std::vector<uint16_t> strip = {0, 1, 1, 2};  // (0,1,1) is degenerate
        std::vector<uint32_t> triangles;
        RockTreeNodeDataParser::StripToTriangleList(strip, triangles);
        
        // (0,1,1) skipped, only (1,2,1) which is also degenerate → 0 triangles
        // Actually (1,1,2) at i=2 even: a=0,b=1,c=1 → degenerate
        // (1,1,2) at i=3 odd: a=1,b=1,c=2 → degenerate
        failed += !Expect(triangles.empty(), "Degenerate triangles should be skipped");
        std::cout << "  Degenerate triangle skip: OK\n";
    }

    // Test 6: Minimal NodeData full parse
    {
        auto data = BuildMinimalNodeData();
        ParsedNodeData result = RockTreeNodeDataParser::Parse(data);
        
        failed += !Expect(result.success, "Minimal data should parse successfully");
        failed += !Expect(result.hasTransform, "Should have transform");
        failed += !Expect(result.vertexCount == 3, "Should have 3 vertices");
        failed += !Expect(result.positions.size() == 9, "Should have 9 position bytes");
        failed += !Expect(result.positions[0] == 0, "V0.x should be 0");
        failed += !Expect(result.positions[3] == 100, "V1.x should be 100");
        failed += !Expect(result.positions[7] == 100, "V2.y should be 100");
        failed += !Expect(result.triangleCount == 1, "Should have 1 triangle");
        failed += !Expect(result.indices.size() == 3, "Should have 3 indices");
        std::cout << "  Minimal NodeData parse: " << result.vertexCount << " verts, "
                  << result.triangleCount << " tris\n";
    }

    // Test 7: Full NodeData with texture + UV quant override
    {
        auto data = BuildFullNodeData();
        ParsedNodeData result = RockTreeNodeDataParser::Parse(data);
        
        failed += !Expect(result.success, "Full data should parse successfully");
        failed += !Expect(result.vertexCount == 4, "Should have 4 vertices");
        failed += !Expect(result.texture.valid, "Should have texture");
        failed += !Expect(result.texture.width == 256, "Texture width should be 256");
        failed += !Expect(result.texture.height == 256, "Texture height should be 256");
        failed += !Expect(result.texture.jpegBytes.size() == 4, "Should have 4 JPEG bytes");
        failed += !Expect(result.triangleCount == 2, "Should have 2 triangles from strip");
        
        // UV quant override from field 10
        failed += !Expect(result.uvQuant.offsetU == 0.0f, "UV offsetU should be 0");
        float expectedScale = 1.0f / 256.0f;
        failed += !Expect(std::abs(result.uvQuant.scaleU - expectedScale) < 1e-6f,
                         "UV scaleU should be 1/256");
        
        std::cout << "  Full NodeData parse: " << result.vertexCount << " verts, "
                  << result.triangleCount << " tris, texture "
                  << result.texture.width << "x" << result.texture.height << "\n";
    }

    // Test 8: Invalid transform length
    {
        std::vector<uint8_t> topLevel;
        AppendVarint(topLevel, (1 << 3) | 2);  // field 1, length-delimited
        AppendVarint(topLevel, 64);             // Wrong: should be 128
        topLevel.resize(topLevel.size() + 64);
        
        ParsedNodeData result = RockTreeNodeDataParser::Parse(topLevel);
        
        failed += !Expect(!result.success, "Wrong transform length should fail");
        failed += !Expect(result.error.find("128") != std::string::npos,
                         "Error should mention expected 128 bytes");
        std::cout << "  Bad transform length error: " << result.error << "\n";
    }

    // Test 9: Unknown field tolerance
    {
        std::vector<uint8_t> topLevel;
        
        // field 3: unknown varint at top level
        topLevel.push_back(MakeFieldKey(3, 0));
        AppendVarint(topLevel, 42);
        
        // field 1: transform
        double identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 6371000,0,0,1};
        AppendFieldRaw(topLevel, 1, reinterpret_cast<const uint8_t*>(identity), 128);
        
        // field 2: minimal mesh
        std::vector<uint8_t> positions = {0,0,0, 10,0,0};
        auto vertData = BuildVertexData(positions);
        std::vector<uint8_t> mesh;
        AppendFieldRaw(mesh, 1, vertData.data(), vertData.size());
        AppendField(topLevel, 2, mesh);
        
        ParsedNodeData result = RockTreeNodeDataParser::Parse(topLevel);
        
        failed += !Expect(result.success, "Should parse with unknown top-level varint");
        failed += !Expect(result.hasTransform, "Should have transform");
        std::cout << "  Unknown field tolerance: OK\n";
    }

    // Test 10: Multi-byte field key (fieldNum >= 16)
    {
        std::vector<uint8_t> topLevel;
        
        // field 1: transform
        double identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 6371000,0,0,1};
        AppendFieldRaw(topLevel, 1, reinterpret_cast<const uint8_t*>(identity), 128);
        
        // field 16: unknown length-delimited (multi-byte varint key)
        std::vector<uint8_t> dummy(4, 0);
        AppendField(topLevel, 16, dummy);
        
        // field 2: minimal mesh
        std::vector<uint8_t> positions = {0,0,0, 10,0,0};
        auto vertData = BuildVertexData(positions);
        std::vector<uint8_t> mesh;
        AppendFieldRaw(mesh, 1, vertData.data(), vertData.size());
        AppendField(topLevel, 2, mesh);
        
        ParsedNodeData result = RockTreeNodeDataParser::Parse(topLevel);
        
        failed += !Expect(result.success, "Should parse with multi-byte field key");
        failed += !Expect(result.hasTransform, "Should have transform");
        std::cout << "  Multi-byte field key: OK\n";
    }

    // Test 11: UvQuantization decode formula
    {
        UvQuantization quant;
        quant.offsetU = 0.5f;
        quant.offsetV = 0.5f;
        quant.scaleU = 1.0f / 256.0f;
        quant.scaleV = 1.0f / 256.0f;
        
        float u, v;
        
        // u16 = 0 => (0 * 1/256) + 0.5 = 0.5
        quant.Decode(0, 0, u, v);
        failed += !Expect(std::abs(u - 0.5f) < 0.001f, "UV decode at 0 should be 0.5 (offset)");
        
        // u16 = 128 => (128 * 1/256) + 0.5 = 1.0
        quant.Decode(128, 128, u, v);
        failed += !Expect(std::abs(u - 1.0f) < 0.001f, "UV decode at 128 should be 1.0");
        
        std::cout << "  UV quant decode: OK\n";
    }
    
    // Test 12: UnpackForNormals + UnpackNormals
    {
        // Build a tiny for_normals palette: 2 normals
        // Format: uint16 count=2, uint8 scale=0, then 2+2 bytes of encoded normal data
        std::vector<uint8_t> forNormalsData;
        uint16_t count = 2;
        forNormalsData.push_back(count & 0xFF);
        forNormalsData.push_back((count >> 8) & 0xFF);
        forNormalsData.push_back(0);  // scale = 0
        // With scale=0, f1(v,0) = v (since 4>=0, result = (v<<0) + (v & 0) = v)
        // So a = data[0+i]/255, f = data[count+i]/255
        // For normal pointing in +X: a≈0.75, f≈0.5 gives approximately (1,0,0)
        forNormalsData.push_back(191);  // a for normal 0
        forNormalsData.push_back(128);  // a for normal 1
        forNormalsData.push_back(128);  // f for normal 0
        forNormalsData.push_back(128);  // f for normal 1
        
        std::vector<uint8_t> palette;
        int paletteCount = 0;
        bool ok = RockTreeNodeDataParser::UnpackForNormals(
            forNormalsData.data(), forNormalsData.size(), palette, paletteCount);
        
        failed += !Expect(ok, "UnpackForNormals should succeed");
        failed += !Expect(paletteCount == 2, "Palette should have 2 entries");
        failed += !Expect(palette.size() == 6, "Palette should be 6 bytes");
        
        // Now test UnpackNormals: 2 vertices pointing to palette entries 0 and 1
        // Format: split layout [low bytes][high bytes]
        uint8_t normData[] = {0, 1,   // low bytes: idx[0]=0, idx[1]=1
                              0, 0};  // high bytes: both 0
        std::vector<uint8_t> outNormals;
        ok = RockTreeNodeDataParser::UnpackNormals(normData, 4, 2, palette, paletteCount, outNormals);
        
        failed += !Expect(ok, "UnpackNormals should succeed");
        failed += !Expect(outNormals.size() == 6, "Should have 6 normal bytes (2 verts * 3)");
        // Vertex 0 should match palette entry 0
        failed += !Expect(outNormals[0] == palette[0] && outNormals[1] == palette[1] && outNormals[2] == palette[2],
                         "V0 normal should match palette[0]");
        // Vertex 1 should match palette entry 1
        failed += !Expect(outNormals[3] == palette[3] && outNormals[4] == palette[4] && outNormals[5] == palette[5],
                         "V1 normal should match palette[1]");
        
        std::cout << "  UnpackForNormals + UnpackNormals: OK\n";
    }

    // Test 13: V-flip fallback when field 7 present but field 10 absent
    {
        // Build NodeData with field 7 (texture_coordinates) but NO field 10
        // The V-flip fallback should apply: offsetV -= 1/scaleV, scaleV *= -1
        
        // 3 vertices
        std::vector<uint8_t> positions = {0,0,0, 100,0,0, 0,100,0};
        auto vertexData = BuildVertexData(positions);
        
        // Build field 7 data: 4-byte header + 3*4 data bytes
        // u_mod-1 = 255 (u_mod = 256), v_mod-1 = 255 (v_mod = 256)
        std::vector<uint8_t> texCoordData;
        uint16_t u_mod_m1 = 255, v_mod_m1 = 255;
        texCoordData.resize(4);
        memcpy(texCoordData.data() + 0, &u_mod_m1, 2);
        memcpy(texCoordData.data() + 2, &v_mod_m1, 2);
        // 3 vertices * 4 bytes (u_lo, v_lo, u_hi, v_hi planar)
        texCoordData.resize(4 + 3 * 4, 0);  // all zero deltas
        
        // Strip
        std::vector<uint16_t> strip = {0, 1, 2};
        auto indexData = BuildIndexData(strip);
        
        // Build mesh with field 7 but NO field 10
        std::vector<uint8_t> mesh;
        AppendFieldRaw(mesh, 1, vertexData.data(), vertexData.size());
        AppendFieldRaw(mesh, 3, indexData.data(), indexData.size());
        AppendFieldRaw(mesh, 7, texCoordData.data(), texCoordData.size());
        // Intentionally NO field 10
        
        // Build top-level
        std::vector<uint8_t> topLevel;
        double tfm[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 6371000,0,0,1};
        AppendFieldRaw(topLevel, 1, reinterpret_cast<const uint8_t*>(tfm), 128);
        AppendField(topLevel, 2, mesh);
        
        ParsedNodeData result = RockTreeNodeDataParser::Parse(topLevel);
        
        failed += !Expect(result.success, "Field7-no-field10 should parse OK");
        failed += !Expect(result.hasTexCoords, "Should have texCoords from field 7");
        failed += !Expect(!result.hasUvOffsetAndScale, "Should NOT have field 10 override");
        
        // UnpackTexCoords computes: scaleV = 1/256 = 0.00390625, offsetV = 0.5
        // V-flip fallback: offsetV -= 1/scaleV = 0.5 - 256 = -255.5
        //                  scaleV *= -1 = -0.00390625
        float expectedScaleV = -1.0f / 256.0f;
        float expectedOffsetV = 0.5f - 256.0f;
        failed += !Expect(std::abs(result.uvQuant.scaleV - expectedScaleV) < 1e-6f,
                         "V-flip fallback should negate scaleV");
        failed += !Expect(std::abs(result.uvQuant.offsetV - expectedOffsetV) < 1e-3f,
                         "V-flip fallback should adjust offsetV");
        
        std::cout << "  V-flip fallback (field7 no field10): scaleV=" 
                  << result.uvQuant.scaleV << " offsetV=" << result.uvQuant.offsetV << " OK\n";
    }
    
    // Test 14: Unaligned buffer UV/forNormals parsing (UB prevention)
    {
        // Create a buffer with 1-byte offset to ensure uint16 reads are unaligned
        std::vector<uint8_t> aligned(32, 0);
        
        // Test UnpackTexCoords with unaligned data
        // Place tex coord header at offset 1 (odd address)
        std::vector<uint8_t> padded(1, 0xAA);  // 1-byte padding
        uint16_t u_mod_m1 = 127, v_mod_m1 = 127;
        padded.resize(padded.size() + 4);
        memcpy(padded.data() + 1, &u_mod_m1, 2);
        memcpy(padded.data() + 3, &v_mod_m1, 2);
        // 2 vertices * 4 bytes data
        padded.resize(padded.size() + 2 * 4, 0);
        
        std::vector<uint16_t> outUV;
        UvQuantization outQuant;
        bool ok = RockTreeNodeDataParser::UnpackTexCoords(
            padded.data() + 1, padded.size() - 1, 2, outUV, outQuant);
        
        failed += !Expect(ok, "UnpackTexCoords with unaligned buffer should succeed");
        failed += !Expect(outUV.size() == 4, "Should have 4 UV values (2 verts)");
        float expectedScale = 1.0f / 128.0f;
        failed += !Expect(std::abs(outQuant.scaleU - expectedScale) < 1e-6f,
                         "Unaligned UV scale should be correct");
        
        // Test UnpackForNormals with unaligned data
        std::vector<uint8_t> normPadded(1, 0xBB);  // 1-byte padding
        uint16_t normCount = 1;
        normPadded.resize(normPadded.size() + 3);
        memcpy(normPadded.data() + 1, &normCount, 2);  // count at odd address
        normPadded[3] = 0;  // scale
        // 1 normal * 2 bytes
        normPadded.push_back(128);  // a
        normPadded.push_back(128);  // f
        
        std::vector<uint8_t> palette;
        int paletteCount = 0;
        ok = RockTreeNodeDataParser::UnpackForNormals(
            normPadded.data() + 1, normPadded.size() - 1, palette, paletteCount);
        
        failed += !Expect(ok, "UnpackForNormals with unaligned buffer should succeed");
        failed += !Expect(paletteCount == 1, "Unaligned palette count should be 1");
        failed += !Expect(palette.size() == 3, "Unaligned palette should have 3 bytes");
        
        std::cout << "  Unaligned buffer parsing: OK\n";
    }

    if (failed == 0) {
        std::cout << "GeNodeDataParserTest PASSED\n";
        return 0;
    }

    std::cerr << "GeNodeDataParserTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
