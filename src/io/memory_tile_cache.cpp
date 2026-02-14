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
    while (map_.size() > maxEntries_ || bytesUsed_ > maxBytes_) {
        if (lru_.empty()) {
            break;
        }

        const CacheKey& victim = lru_.back();
        
        // P2: Pin'lenmiş entry'leri asla silme (O(1) lookup)
        if (pinned_.find(victim) != pinned_.end()) {
            // Tümü pin'liyse çık (deadlock önleme)
            bool allPinned = std::all_of(lru_.begin(), lru_.end(),
                [&](const CacheKey& k) { return pinned_.find(k) != pinned_.end(); });
            if (allPinned) {
                break;  // Hepsi pin'li, daha fazla eviction yapma
            }
            
            // Sonraki victim'a geç
            lru_.splice(lru_.begin(), lru_, std::prev(lru_.end()));
            continue;
        }
        
        auto it = map_.find(victim);
        if (it == map_.end()) {
            lru_.pop_back();
            continue;
        }

        bytesUsed_ -= it->second.data.size();
        map_.erase(it);
        lru_.pop_back();
        ++stats_.evictions;
    }
}

} // namespace globe
