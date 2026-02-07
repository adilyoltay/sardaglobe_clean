#pragma once

#include "../core/tile_key.h"
#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace globe {

// In-memory compressed tile cache (LRU, thread-safe)
class MemoryTileCache {
public:
    struct Stats {
        size_t readHits = 0;
        size_t readMisses = 0;
        size_t writeSuccess = 0;
        size_t writeReject = 0;
        size_t evictions = 0;
        size_t bytesUsed = 0;
        size_t entryCount = 0;
    };

    MemoryTileCache(size_t maxEntries, size_t maxBytes);

    bool Read(const TileKey& key, const std::string& urlTemplate, std::vector<uint8_t>& out);
    bool Write(const TileKey& key, const std::string& urlTemplate, const std::vector<uint8_t>& data);
    bool Remove(const TileKey& key, const std::string& urlTemplate);
    void Clear();

    Stats GetStats() const;

private:
    struct CacheKey {
        TileKey key;
        size_t sourceHash = 0;

        bool operator==(const CacheKey& other) const {
            return key == other.key && sourceHash == other.sourceHash;
        }
    };

    struct CacheKeyHash {
        size_t operator()(const CacheKey& k) const noexcept {
            size_t h1 = TileKey::Hash{}(k.key);
            size_t h2 = std::hash<size_t>{}(k.sourceHash);
            return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
        }
    };

    struct Entry {
        std::vector<uint8_t> data;
        std::list<CacheKey>::iterator lruIt;
    };

    static size_t HashSource(const std::string& urlTemplate);
    static CacheKey MakeCacheKey(const TileKey& key, const std::string& urlTemplate);
    void EvictIfNeededLocked();

    size_t maxEntries_ = 0;
    size_t maxBytes_ = 0;
    size_t bytesUsed_ = 0;

    mutable std::mutex mutex_;
    std::unordered_map<CacheKey, Entry, CacheKeyHash> map_;
    std::list<CacheKey> lru_; // front = most-recently-used

    Stats stats_;
};

} // namespace globe
