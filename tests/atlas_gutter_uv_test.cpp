// Atlas gutter + UV inset regression test.

#include "../src/rendering/texture_atlas_allocator.h"
#include <cmath>
#include <iostream>

using namespace globe;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

bool Near(float a, float b, float eps = 1e-6f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    int failed = 0;

    TextureAtlasAllocator allocator(1024, 256, 2);
    failed += !Expect(allocator.IsValid(), "allocator should be valid");
    failed += !Expect(allocator.GetSlotFootprint() == 260, "slot footprint should include gutters");

    TextureAtlasAllocation a;
    TextureAtlasAllocation b;
    failed += !Expect(allocator.Allocate(256, 256, a), "allocation A should succeed");
    failed += !Expect(allocator.Allocate(256, 256, b), "allocation B should succeed");
    failed += !Expect(a.slotIndex != b.slotIndex, "A and B should occupy different slots");

    glm::vec4 uvA = allocator.ToUvTransform(a);
    glm::vec4 uvB = allocator.ToUvTransform(b);

    const float atlasSize = static_cast<float>(allocator.GetAtlasSize());
    float aMinPx = uvA.z * atlasSize;
    float aMaxPx = (uvA.z + uvA.x) * atlasSize;
    float bMinPx = uvB.z * atlasSize;
    float bMaxPx = (uvB.z + uvB.x) * atlasSize;

    failed += !Expect(Near(aMinPx, static_cast<float>(a.x) + 0.5f), "A min pixel should be half-texel inset");
    failed += !Expect(Near(aMaxPx, static_cast<float>(a.x + a.width) - 0.5f), "A max pixel should be half-texel inset");
    failed += !Expect(Near(bMinPx, static_cast<float>(b.x) + 0.5f), "B min pixel should be half-texel inset");
    failed += !Expect(Near(bMaxPx, static_cast<float>(b.x + b.width) - 0.5f), "B max pixel should be half-texel inset");

    // Neighbor slot boundary should preserve a positive gap due to gutter.
    float rightEdgeA = static_cast<float>(a.x + a.width) - 0.5f;
    float leftEdgeB = static_cast<float>(b.x) + 0.5f;
    failed += !Expect(leftEdgeB > rightEdgeA, "neighbor slots should have a positive sampling gap");

    if (failed == 0) {
        std::cout << "AtlasGutterUvTest PASSED\n";
        return 0;
    }

    std::cerr << "AtlasGutterUvTest FAILED (" << failed << " checks failed)\n";
    return 1;
}

