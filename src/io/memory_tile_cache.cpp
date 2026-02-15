#include "memory_tile_cache.h"
#include <algorithm>
#include <functional>

namespace globe {

MemoryTileCache::MemoryTileCache(size_t maxEntries, size_t maxBytes)
    : maxEntries_(std::max<size_t>(1, maxEntries))
    , maxBytes_(std::max<size_t>(1, maxBytes)) {}

size_t MemoryTileCache::HashSource(const std::string& urlTemplate) {
    return std::hash<std::string>{}(urlTemplate);
}

MemoryTileCache::CacheKey MemoryTileCache::MakeCacheKey(const TileKey& key, const std::string& urlTemplate) {
    return CacheKey{key, HashSource(urlTemplate)};
}

bool MemoryTileCache::Read(const TileKey& key, const std::string& urlTemplate, std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    CacheKey ckey = MakeCacheKey(key, urlTemplate);
    auto it = map_.find(ckey);
    if (it == map_.end()) {
        ++stats_.readMisses;
        return false;
    }

    lru_.splice(lru_.begin(), lru_, it->second.lruIt);
    out = it->second.data;
    ++stats_.readHits;
    return true;
}

bool MemoryTileCache::Write(const TileKey& key, const std::string& urlTemplate, const std::vector<uint8_t>& data) {
    if (data.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.writeReject;
        return false;
    }

    if (data.size() > maxBytes_) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.writeReject;
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    CacheKey ckey = MakeCacheKey(key, urlTemplate);
    auto it = map_.find(ckey);
    if (it != map_.end()) {
        bytesUsed_ -= it->second.data.size();
        it->second.data = data;
        bytesUsed_ += it->second.data.size();
        lru_.splice(lru_.begin(), lru_, it->second.lruIt);
    } else {
        lru_.push_front(ckey);
        Entry entry;
        entry.data = data;
        entry.lruIt = lru_.begin();
        bytesUsed_ += entry.data.size();
        map_.emplace(std::move(ckey), std::move(entry));
    }

    ++stats_.writeSuccess;
    EvictIfNeededLocked();
    return true;
}

bool MemoryTileCache::Remove(const TileKey& key, const std::string& urlTemplate) {
    std::lock_guard<std::mutex> lock(mutex_);
    CacheKey ckey = MakeCacheKey(key, urlTemplate);
    auto it = map_.find(ckey);
    if (it == map_.end()) {
        return false;
    }

    bytesUsed_ -= it->second.data.size();
    lru_.erase(it->second.lruIt);
    map_.erase(it);
    return true;
}

void MemoryTileCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    map_.clear();
    lru_.clear();
    pinned_.clear();  // P2: Pin temizliği eklendi
    bytesUsed_ = 0;
}

MemoryTileCache::Stats MemoryTileCache::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats out = stats_;
    out.bytesUsed = bytesUsed_;
    out.entryCount = map_.size();
    return out;
}

void MemoryTileCache::Pin(const TileKey& key, const std::string& urlTemplate) {
    std::lock_guard<std::mutex> lock(mutex_);
    pinned_.insert(MakeCacheKey(key, urlTemplate));  // O(1) insert
}

void MemoryTileCache::Unpin(const TileKey& key, const std::string& urlTemplate) {
    std::lock_guard<std::mutex> lock(mutex_);
    pinned_.erase(MakeCacheKey(key, urlTemplate));  // O(1) erase
}

void MemoryTileCache::UnpinAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    pinned_.clear();
}

size_t MemoryTileCache::GetPinnedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pinned_.size();
}

void MemoryTileCache::EvictIfNeededLocked() {
    const auto isPinnedInCache = [this](const CacheKey& key) {
        return pinned_.find(key) != pinned_.end();
    };

    // Track how many cache entries are currently pinned.
    size_t pinnedInCacheCount = 0;
    for (const auto& pair : map_) {
        const CacheKey& key = pair.first;
        if (isPinnedInCache(key)) {
            ++pinnedInCacheCount;
        }
    }

    while (map_.size() > maxEntries_ || bytesUsed_ > maxBytes_) {
        if (lru_.empty()) {
            break;
        }

        // All entries are pinned -> no legal victim exists.
        if (pinnedInCacheCount >= map_.size()) {
            break;
        }

        auto it = std::prev(lru_.end());
        const CacheKey& victimKey = *it;

        if (isPinnedInCache(victimKey)) {
            // Move pinned tail entries forward so stale pins are crossed quickly
            // and we keep scanning old unpinned candidates in O(N).
            lru_.splice(lru_.begin(), lru_, it);
            continue;
        }

        auto mapIt = map_.find(victimKey);
        if (mapIt == map_.end()) {
            lru_.erase(it);
            continue;
        }

        bytesUsed_ -= mapIt->second.data.size();
        map_.erase(mapIt);
        lru_.erase(it);
        ++stats_.evictions;
    }

    // Remove any stale LRU nodes that were never synchronized from map state.
    for (auto it = lru_.begin(); it != lru_.end();) {
        if (map_.find(*it) == map_.end()) {
            it = lru_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace globe
