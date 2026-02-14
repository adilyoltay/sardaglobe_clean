#include "node_data_cache.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <mutex>
#include <chrono>
#include <cstring>

// Simple SHA-256-like hash for cache key generation
// Using a simple FNV-1a hash for speed (sufficient for cache keys)
namespace {
    
uint64_t FNV1aHash(const std::string& data) {
    const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    const uint64_t FNV_PRIME = 1099511628211ULL;
    
    uint64_t hash = FNV_OFFSET_BASIS;
    for (char c : data) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= FNV_PRIME;
    }
    return hash;
}

std::string HashToHex(uint64_t hash) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

std::string GetMetadataPath(const std::string& dataPath) {
    return dataPath + ".meta";
}

} // anonymous namespace

namespace globe {

NodeDataDiskCache::NodeDataDiskCache(const std::string& cacheDir)
    : cacheDir_(cacheDir) {
}

bool NodeDataDiskCache::Init() {
    try {
        std::filesystem::create_directories(cacheDir_);
        return std::filesystem::exists(cacheDir_);
    } catch (const std::exception& e) {
        return false;
    }
}

bool NodeDataDiskCache::Contains(const std::string& endpoint, 
                                  const std::string& nodeKey) const {
    std::string path = GetCacheFilePath(endpoint, nodeKey);
    return std::filesystem::exists(path);
}

// Sprint 3: Hardened read with size validation
std::vector<uint8_t> NodeDataDiskCache::Read(const std::string& endpoint, 
                                              const std::string& nodeKey) {
    std::string path = GetCacheFilePath(endpoint, nodeKey);
    
    // Check if file exists
    if (!std::filesystem::exists(path)) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.missCount++;
        return {};
    }
    
    // Get actual file size
    uint64_t actualFileSize = 0;
    try {
        actualFileSize = std::filesystem::file_size(path);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.missCount++;
        stats_.errorCount++;
        return {};
    }
    
    // Sprint 3: Validate file size is reasonable (not negative, not huge)
    if (actualFileSize == 0 || actualFileSize > 100 * 1024 * 1024) {
        // Empty file or >100MB - treat as corrupt
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.missCount++;
        stats_.errorCount++;
        // Remove corrupt file
        try { std::filesystem::remove(path); } catch (...) {}
        return {};
    }
    
    // Read metadata to verify validity
    auto meta = ReadMetadata(path);
    if (!meta || !meta->valid) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.missCount++;
        return {};
    }
    
    // Sprint 3: Validate size matches metadata
    if (meta->sizeBytes > 0 && meta->sizeBytes != actualFileSize) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.missCount++;
        stats_.errorCount++;
        // Remove corrupt cache entry
        try { 
            std::filesystem::remove(path); 
            std::filesystem::remove(GetMetadataPath(path));
        } catch (...) {}
        return {};
    }
    
    // Read data file
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.missCount++;
        stats_.errorCount++;
        return {};
    }
    
    // Verify stream size matches
    std::streamsize size = file.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) != actualFileSize) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.missCount++;
        stats_.errorCount++;
        return {};
    }
    
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.missCount++;
        stats_.errorCount++;
        return {};
    }
    
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.hitCount++;
    return data;
}

bool NodeDataDiskCache::Write(const std::string& endpoint, 
                               const std::string& nodeKey,
                               const std::vector<uint8_t>& data) {
    if (data.empty()) {
        return false;
    }
    
    std::string path = GetCacheFilePath(endpoint, nodeKey);
    
    // Ensure parent directory exists
    std::filesystem::path parentPath = std::filesystem::path(path).parent_path();
    if (!EnsureDirectory(parentPath.string())) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.errorCount++;
        return false;
    }
    
    // Write data to temp file first, then rename for atomicity
    std::string tempPath = path + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary);
        if (!file.is_open()) {
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.errorCount++;
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        if (!file.good()) {
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.errorCount++;
            std::filesystem::remove(tempPath);
            return false;
        }
    }
    
    // Atomic rename
    try {
        std::filesystem::rename(tempPath, path);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.errorCount++;
        std::filesystem::remove(tempPath);
        return false;
    }
    
    // Write metadata
    NodeDataCacheEntry entry;
    entry.nodeKey = nodeKey;
    entry.endpointHash = HashToHex(FNV1aHash(endpoint));
    entry.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    entry.sizeBytes = data.size();
    entry.valid = true;
    
    if (!WriteMetadata(path, entry)) {
        // Non-critical: entry is cached but metadata failed
    }
    
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.writeCount++;
    stats_.totalBytesStored += data.size();
    return true;
}

bool NodeDataDiskCache::Remove(const std::string& endpoint, 
                                const std::string& nodeKey) {
    std::string path = GetCacheFilePath(endpoint, nodeKey);
    
    bool success = true;
    try {
        if (std::filesystem::exists(path)) {
            std::filesystem::remove(path);
        }
        std::string metaPath = GetMetadataPath(path);
        if (std::filesystem::exists(metaPath)) {
            std::filesystem::remove(metaPath);
        }
    } catch (const std::exception& e) {
        success = false;
    }
    
    return success;
}

bool NodeDataDiskCache::Clear() {
    try {
        if (std::filesystem::exists(cacheDir_)) {
            std::filesystem::remove_all(cacheDir_);
        }
        return EnsureDirectory(cacheDir_);
    } catch (const std::exception& e) {
        return false;
    }
}

NodeDataDiskCache::Stats NodeDataDiskCache::GetStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

std::string NodeDataDiskCache::GetCacheFilePath(const std::string& endpoint,
                                                 const std::string& nodeKey) const {
    std::string key = GenerateCacheKey(endpoint, nodeKey);
    
    // Use first 2 chars as bucket subdirectory for better filesystem performance
    std::string bucket = key.substr(0, 2);
    
    return cacheDir_ + "/" + bucket + "/" + key + ".bin";
}

std::string NodeDataDiskCache::GenerateCacheKey(const std::string& endpoint,
                                                 const std::string& nodeKey) const {
    // Combine endpoint and nodeKey for unique cache key
    std::string combined = endpoint + ":" + nodeKey;
    return HashToHex(FNV1aHash(combined));
}

bool NodeDataDiskCache::EnsureDirectory(const std::string& path) const {
    try {
        std::filesystem::create_directories(path);
        return std::filesystem::exists(path);
    } catch (const std::exception& e) {
        return false;
    }
}

bool NodeDataDiskCache::WriteMetadata(const std::string& path, 
                                       const NodeDataCacheEntry& entry) {
    std::string metaPath = GetMetadataPath(path);
    
    std::ofstream file(metaPath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Simple binary format: version(1) + timestamp(8) + size(8) + nodeKeyLen(2) + nodeKey + valid(1)
    file.put(1);  // version
    
    uint64_t timestamp = entry.timestampMs;
    file.write(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
    
    uint64_t size = entry.sizeBytes;
    file.write(reinterpret_cast<const char*>(&size), sizeof(size));
    
    uint16_t keyLen = static_cast<uint16_t>(entry.nodeKey.length());
    file.write(reinterpret_cast<const char*>(&keyLen), sizeof(keyLen));
    file.write(entry.nodeKey.data(), keyLen);
    
    file.put(entry.valid ? 1 : 0);
    
    return file.good();
}

// Sprint 2.3: Hardened metadata reading with validation
std::optional<NodeDataCacheEntry> NodeDataDiskCache::ReadMetadata(const std::string& path) {
    std::string metaPath = GetMetadataPath(path);
    
    if (!std::filesystem::exists(metaPath)) {
        // No metadata - assume valid (backward compatibility)
        NodeDataCacheEntry entry;
        entry.valid = true;
        return entry;
    }
    
    // Check file size (must be at least: version(1) + timestamp(8) + size(8) + keyLen(2) + valid(1) = 20 bytes)
    constexpr size_t MIN_META_SIZE = 1 + 8 + 8 + 2 + 1;
    constexpr size_t MAX_KEY_LEN = 256;  // Reasonable limit for nodeKey
    
    try {
        size_t fileSize = std::filesystem::file_size(metaPath);
        if (fileSize < MIN_META_SIZE) {
            return std::nullopt;  // File too small
        }
    } catch (const std::exception& e) {
        return std::nullopt;
    }
    
    std::ifstream file(metaPath, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }
    
    int version = file.get();
    if (version != 1 || file.gcount() != 1) {
        return std::nullopt;  // Invalid version or read failed
    }
    
    NodeDataCacheEntry entry;
    
    // Read timestamp
    uint64_t timestamp;
    file.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));
    if (file.gcount() != sizeof(timestamp)) {
        return std::nullopt;  // Partial read
    }
    entry.timestampMs = timestamp;
    
    // Read size
    uint64_t size;
    file.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (file.gcount() != sizeof(size)) {
        return std::nullopt;  // Partial read
    }
    entry.sizeBytes = size;
    
    // Read key length
    uint16_t keyLen;
    file.read(reinterpret_cast<char*>(&keyLen), sizeof(keyLen));
    if (file.gcount() != sizeof(keyLen)) {
        return std::nullopt;  // Partial read
    }
    
    // Validate key length
    if (keyLen > MAX_KEY_LEN) {
        return std::nullopt;  // Key too long (possible corruption)
    }
    
    // Read nodeKey
    entry.nodeKey.resize(keyLen);
    if (keyLen > 0) {
        file.read(entry.nodeKey.data(), keyLen);
        if (static_cast<uint16_t>(file.gcount()) != keyLen) {
            return std::nullopt;  // Partial read
        }
    }
    
    // Read valid flag
    int valid = file.get();
    if (file.gcount() != 1) {
        return std::nullopt;  // Read failed
    }
    entry.valid = (valid != 0);
    
    // Verify no extra unexpected data (optional strict check)
    // file.peek(); if (file.gcount() > 0) return std::nullopt;
    
    return entry;
}

} // namespace globe
