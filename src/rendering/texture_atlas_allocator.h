#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace globe {

struct TextureAtlasAllocation {
    int pageIndex = -1;
    int slotIndex = -1;
    int slotX = 0;      // Slot origin (including gutter area)
    int slotY = 0;
    int slotSpan = 0;   // Full slot size: content + 2*gutter
    int gutterPx = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool IsValid() const {
        return pageIndex >= 0 && slotIndex >= 0 && width > 0 && height > 0;
    }
};

// Fixed-size slot atlas allocator (first P3.2 technical slice).
// Each atlas page is divided into slotSize x slotSize cells.
class TextureAtlasAllocator {
public:
    TextureAtlasAllocator(int atlasSize, int slotSize, int gutterPx = 0);

    bool IsValid() const;

    bool Allocate(int width, int height, TextureAtlasAllocation& outAllocation);
    bool Resolve(int pageIndex, int slotIndex, int width, int height, TextureAtlasAllocation& outAllocation) const;
    bool Free(const TextureAtlasAllocation& allocation);
    int TrimTrailingEmptyPages();

    glm::vec4 ToUvTransform(const TextureAtlasAllocation& allocation) const;

    int GetAtlasSize() const { return atlasSize_; }
    int GetSlotSize() const { return slotSize_; }
    int GetGutterPx() const { return gutterPx_; }
    int GetSlotFootprint() const { return slotFootprint_; }
    int GetPageCount() const { return static_cast<int>(pages_.size()); }
    int GetSlotsPerPage() const { return slotsPerPage_; }
    int GetUsedSlots() const { return usedSlots_; }
    int GetTotalCapacity() const { return GetPageCount() * slotsPerPage_; }
    int GetPageUsedSlots(int pageIndex) const;

private:
    struct Page {
        std::vector<unsigned char> occupied;
        int usedSlots = 0;
    };

    TextureAtlasAllocation MakeAllocation(int pageIndex, int slotIndex, int width, int height) const;

    int atlasSize_ = 0;
    int slotSize_ = 0;
    int gutterPx_ = 0;
    int slotFootprint_ = 0;
    int cols_ = 0;
    int rows_ = 0;
    int slotsPerPage_ = 0;
    int usedSlots_ = 0;
    std::vector<Page> pages_;
};

} // namespace globe
