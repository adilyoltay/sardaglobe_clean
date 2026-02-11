#include "tile_cache.h"
#include <fstream>
#include <functional>

namespace globe {

TileCache::TileCache(const std::string& cacheDir) 
    : cacheDir_(cacheDir) {
    std::error_code ec;
    std::filesystem::create_directories(cacheDir_, ec);
}

std::string TileCache::HashUrl(const std::string& url) const {
    // Simple hash for URL to create directory structure
    std::hash<std::string> hasher;
    size_t h = hasher(url);
    return std::to_string(h % 1000);  // 1000 buckets
}

std::filesystem::path TileCache::GetPath(const TileKey& key, const std::string& urlTemplate) const {
    std::string bucket = HashUrl(urlTemplate);
    return cacheDir_ / bucket / std::to_string(key.level) /
           std::to_string(key.x) / (std::to_string(key.y) + ".tile");
}

bool TileCache::Has(const TileKey& key, const std::string& urlTemplate) const {
    if (!enabled_) return false;
    return std::filesystem::exists(GetPath(key, urlTemplate));
}

bool TileCache::Read(const TileKey& key, const std::string& urlTemplate, std::vector<uint8_t>& out) const {
    if (!enabled_) return false;
    
    auto path = GetPath(key, urlTemplate);
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        readMisses_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    out.resize(size);
    file.read(reinterpret_cast<char*>(out.data()), size);
    if (static_cast<bool>(file)) {
        readHits_.fetch_add(1, std::memory_order_relaxed);
        bytesRead_.fetch_add(size, std::memory_order_relaxed);
        return true;
    }

    readMisses_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool TileCache::Write(const TileKey& key, const std::string& urlTemplate, const std::vector<uint8_t>& data) {
    if (!enabled_ || data.empty()) {
        writeFail_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    auto path = GetPath(key, urlTemplate);
    
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        writeFail_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        writeFail_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (static_cast<bool>(file)) {
        writeSuccess_.fetch_add(1, std::memory_order_relaxed);
        bytesWritten_.fetch_add(data.size(), std::memory_order_relaxed);
        return true;
    }

    writeFail_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool TileCache::Remove(const TileKey& key, const std::string& urlTemplate) {
    if (!enabled_) return false;
    std::error_code ec;
    return std::filesystem::remove(GetPath(key, urlTemplate), ec);
}

void TileCache::Clear() {
    std::error_code ec;
    std::filesystem::remove_all(cacheDir_, ec);
    std::filesystem::create_directories(cacheDir_, ec);
}

size_t TileCache::GetSize() const {
    size_t total = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(cacheDir_, ec)) {
        if (entry.is_regular_file()) {
            total += entry.file_size();
        }
    }
    return total;
}

TileCache::Stats TileCache::GetStats() const {
    Stats stats;
    stats.readHits = readHits_.load(std::memory_order_relaxed);
    stats.readMisses = readMisses_.load(std::memory_order_relaxed);
    stats.writeSuccess = writeSuccess_.load(std::memory_order_relaxed);
    stats.writeFail = writeFail_.load(std::memory_order_relaxed);
    stats.bytesRead = bytesRead_.load(std::memory_order_relaxed);
    stats.bytesWritten = bytesWritten_.load(std::memory_order_relaxed);
    return stats;
}

void TileCache::ResetStats() {
    readHits_.store(0, std::memory_order_relaxed);
    readMisses_.store(0, std::memory_order_relaxed);
    writeSuccess_.store(0, std::memory_order_relaxed);
    writeFail_.store(0, std::memory_order_relaxed);
    bytesRead_.store(0, std::memory_order_relaxed);
    bytesWritten_.store(0, std::memory_order_relaxed);
}

} // namespace globe
