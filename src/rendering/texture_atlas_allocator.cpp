#include "texture_atlas_allocator.h"

#include <algorithm>

namespace globe {

TextureAtlasAllocator::TextureAtlasAllocator(int atlasSize, int slotSize, int gutterPx)
    : atlasSize_(std::max(0, atlasSize))
    , slotSize_(std::max(0, slotSize))
    , gutterPx_(std::max(0, gutterPx)) {
    slotFootprint_ = slotSize_ + gutterPx_ * 2;
    if (atlasSize_ <= 0 || slotSize_ <= 0 || slotFootprint_ <= 0 || atlasSize_ < slotFootprint_) {
        return;
    }

    cols_ = atlasSize_ / slotFootprint_;
    rows_ = atlasSize_ / slotFootprint_;
    slotsPerPage_ = cols_ * rows_;
}

bool TextureAtlasAllocator::IsValid() const {
    return atlasSize_ > 0 &&
           slotSize_ > 0 &&
           slotFootprint_ > 0 &&
           cols_ > 0 &&
           rows_ > 0 &&
           slotsPerPage_ > 0;
}

TextureAtlasAllocation TextureAtlasAllocator::MakeAllocation(int pageIndex, int slotIndex, int width, int height) const {
    TextureAtlasAllocation allocation;
    allocation.pageIndex = pageIndex;
    allocation.slotIndex = slotIndex;
    allocation.slotSpan = slotFootprint_;
    allocation.gutterPx = gutterPx_;
    allocation.width = width;
    allocation.height = height;
    allocation.slotX = (slotIndex % cols_) * slotFootprint_;
    allocation.slotY = (slotIndex / cols_) * slotFootprint_;
    allocation.x = allocation.slotX + gutterPx_;
    allocation.y = allocation.slotY + gutterPx_;
    return allocation;
}

bool TextureAtlasAllocator::Resolve(int pageIndex, int slotIndex, int width, int height, TextureAtlasAllocation& outAllocation) const {
    if (!IsValid()) {
        return false;
    }
    if (width <= 0 || height <= 0 || width > slotSize_ || height > slotSize_) {
        return false;
    }
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pages_.size())) {
        return false;
    }
    if (slotIndex < 0 || slotIndex >= slotsPerPage_) {
        return false;
    }
    if (pages_[pageIndex].occupied[slotIndex] == 0) {
        return false;
    }

    outAllocation = MakeAllocation(pageIndex, slotIndex, width, height);
    return true;
}

bool TextureAtlasAllocator::Allocate(int width, int height, TextureAtlasAllocation& outAllocation) {
    if (!IsValid()) {
        return false;
    }
    if (width <= 0 || height <= 0 || width > slotSize_ || height > slotSize_) {
        return false;
    }

    for (int pageIndex = 0; pageIndex < static_cast<int>(pages_.size()); ++pageIndex) {
        Page& page = pages_[pageIndex];
        if (page.usedSlots >= slotsPerPage_) {
            continue;
        }
        for (int slotIndex = 0; slotIndex < slotsPerPage_; ++slotIndex) {
            if (page.occupied[slotIndex] != 0) {
                continue;
            }
            page.occupied[slotIndex] = 1;
            ++page.usedSlots;
            ++usedSlots_;
            outAllocation = MakeAllocation(pageIndex, slotIndex, width, height);
            return true;
        }
    }

    Page page;
    page.occupied.resize(static_cast<size_t>(slotsPerPage_), 0);
    page.occupied[0] = 1;
    page.usedSlots = 1;
    pages_.push_back(std::move(page));
    ++usedSlots_;
    outAllocation = MakeAllocation(static_cast<int>(pages_.size()) - 1, 0, width, height);
    return true;
}

bool TextureAtlasAllocator::Free(const TextureAtlasAllocation& allocation) {
    if (!IsValid() || !allocation.IsValid()) {
        return false;
    }
    if (allocation.pageIndex < 0 || allocation.pageIndex >= static_cast<int>(pages_.size())) {
        return false;
    }
    if (allocation.slotIndex < 0 || allocation.slotIndex >= slotsPerPage_) {
        return false;
    }

    Page& page = pages_[allocation.pageIndex];
    if (page.occupied[allocation.slotIndex] == 0) {
        return false;
    }

    page.occupied[allocation.slotIndex] = 0;
    page.usedSlots = std::max(0, page.usedSlots - 1);
    usedSlots_ = std::max(0, usedSlots_ - 1);
    return true;
}

int TextureAtlasAllocator::TrimTrailingEmptyPages() {
    int removed = 0;
    while (!pages_.empty() && pages_.back().usedSlots == 0) {
        pages_.pop_back();
        ++removed;
    }
    return removed;
}

int TextureAtlasAllocator::GetPageUsedSlots(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pages_.size())) {
        return 0;
    }
    return pages_[pageIndex].usedSlots;
}

glm::vec4 TextureAtlasAllocator::ToUvTransform(const TextureAtlasAllocation& allocation) const {
    if (!IsValid() || !allocation.IsValid()) {
        return glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);
    }

    float invSize = 1.0f / static_cast<float>(atlasSize_);
    float minX = static_cast<float>(allocation.x);
    float minY = static_cast<float>(allocation.y);
    float maxX = static_cast<float>(allocation.x + allocation.width);
    float maxY = static_cast<float>(allocation.y + allocation.height);

    // Inset by half texel to avoid sampling neighboring atlas slots at boundaries.
    if (allocation.width > 1) {
        minX += 0.5f;
        maxX -= 0.5f;
    }
    if (allocation.height > 1) {
        minY += 0.5f;
        maxY -= 0.5f;
    }

    return glm::vec4(
        std::max(0.0f, maxX - minX) * invSize,
        std::max(0.0f, maxY - minY) * invSize,
        minX * invSize,
        minY * invSize
    );
}

} // namespace globe
