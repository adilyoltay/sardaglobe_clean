// Texture Atlas Allocator Test
// Verifies slot allocation/free and UV remap transform generation.

#include "../src/rendering/texture_atlas_allocator.h"
#include <cmath>
#include <iostream>
#include <vector>

using namespace globe;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

bool NearlyEqual(float a, float b, float eps = 1e-6f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

int main() {
    int failed = 0;

    TextureAtlasAllocator allocator(1024, 256);  // 4x4 slots per page.
    failed += !Expect(allocator.IsValid(), "allocator should be valid");
    failed += !Expect(allocator.GetSlotsPerPage() == 16, "slots/page should be 16");

    std::vector<TextureAtlasAllocation> allocations;
    allocations.reserve(17);

    for (int i = 0; i < 16; ++i) {
        TextureAtlasAllocation allocation;
        failed += !Expect(allocator.Allocate(256, 256, allocation), "allocation should succeed");
        failed += !Expect(allocation.pageIndex == 0, "first 16 allocations should use page 0");
        allocations.push_back(allocation);
    }

    TextureAtlasAllocation pageTwoAllocation;
    failed += !Expect(allocator.Allocate(256, 256, pageTwoAllocation), "17th allocation should succeed");
    failed += !Expect(pageTwoAllocation.pageIndex == 1, "17th allocation should spill to page 1");

    TextureAtlasAllocation resolved;
    failed += !Expect(
        allocator.Resolve(allocations[0].pageIndex, allocations[0].slotIndex, 256, 256, resolved),
        "resolve should succeed for occupied slot");
    failed += !Expect(resolved.x == 0 && resolved.y == 0, "slot 0 should be top-left");

    glm::vec4 uv = allocator.ToUvTransform(resolved);
    failed += !Expect(NearlyEqual(uv.x, 255.0f / 1024.0f), "uv scale x should include half-texel inset");
    failed += !Expect(NearlyEqual(uv.y, 255.0f / 1024.0f), "uv scale y should include half-texel inset");
    failed += !Expect(NearlyEqual(uv.z, 0.5f / 1024.0f), "uv offset x should include half-texel inset");
    failed += !Expect(NearlyEqual(uv.w, 0.5f / 1024.0f), "uv offset y should include half-texel inset");

    failed += !Expect(allocator.Free(allocations[5]), "free should succeed");

    TextureAtlasAllocation reused;
    failed += !Expect(allocator.Allocate(128, 128, reused), "allocation after free should succeed");
    failed += !Expect(reused.pageIndex == allocations[5].pageIndex, "reused allocation should stay on same page");
    failed += !Expect(reused.slotIndex == allocations[5].slotIndex, "free slot should be reused");

    TextureAtlasAllocation tooLarge;
    failed += !Expect(!allocator.Allocate(300, 300, tooLarge), "oversized tile should be rejected");

    failed += !Expect(allocator.GetUsedSlots() == 17, "used slot count should match allocations");
    failed += !Expect(allocator.GetPageCount() == 2, "page count should stay at 2");
    failed += !Expect(allocator.GetPageUsedSlots(0) == 16, "page 0 should be full");
    failed += !Expect(allocator.GetPageUsedSlots(1) == 1, "page 1 should have one slot");

    failed += !Expect(allocator.Free(pageTwoAllocation), "free page 1 allocation should succeed");
    failed += !Expect(allocator.TrimTrailingEmptyPages() == 1, "trim should remove one empty trailing page");
    failed += !Expect(allocator.GetPageCount() == 1, "page count should drop to one after trim");

    // Gutter-aware allocation should reserve larger footprints and keep content origin inset.
    TextureAtlasAllocator gutterAllocator(1024, 256, 2);
    failed += !Expect(gutterAllocator.IsValid(), "gutter allocator should be valid");
    TextureAtlasAllocation gutterAlloc;
    failed += !Expect(gutterAllocator.Allocate(256, 256, gutterAlloc), "gutter allocation should succeed");
    failed += !Expect(gutterAlloc.slotSpan == 260, "slot span should include 2px gutter on both sides");
    failed += !Expect(gutterAlloc.x == 2 && gutterAlloc.y == 2, "content origin should be inset by gutter");
    glm::vec4 gutterUv = gutterAllocator.ToUvTransform(gutterAlloc);
    failed += !Expect(gutterUv.z > 0.0f && gutterUv.w > 0.0f, "gutter uv offset should be positive");

    if (failed == 0) {
        std::cout << "TextureAtlasAllocatorTest PASSED\n";
        return 0;
    }

    std::cerr << "TextureAtlasAllocatorTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
