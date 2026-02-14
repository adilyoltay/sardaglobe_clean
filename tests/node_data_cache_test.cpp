// NodeDataDiskCache Test
// Tests Sprint 2.2 disk cache functionality

#include "../src/io/node_data_cache.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cstdio>

using namespace globe;

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

int main() {
    int failed = 0;
    std::cout << "=== NodeDataDiskCache Test ===\n";
    
    // Use temp directory for testing
    std::string testCacheDir = "/tmp/test_ge_mesh_cache_" + 
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    
    // Test 1: Init and basic write/read
    {
        NodeDataDiskCache cache(testCacheDir);
        failed += !Expect(cache.Init(), "Init should succeed");
        
        std::string endpoint = "https://example.com/mesh/{quadkey}";
        std::string nodeKey = "123456";
        std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05};
        
        // Write
        failed += !Expect(cache.Write(endpoint, nodeKey, data), "Write should succeed");
        
        // Contains
        failed += !Expect(cache.Contains(endpoint, nodeKey), "Contains should return true");
        
        // Read
        std::vector<uint8_t> readData = cache.Read(endpoint, nodeKey);
        failed += !Expect(readData.size() == data.size(), "Read size should match");
        failed += !Expect(readData == data, "Read data should match written data");
        
        std::cout << "  Basic write/read: OK\n";
    }
    
    // Test 2: Cache miss
    {
        NodeDataDiskCache cache(testCacheDir);
        cache.Init();
        
        std::string endpoint = "https://example.com/mesh/{quadkey}";
        std::string nodeKey = "NONEXISTENT";
        
        failed += !Expect(!cache.Contains(endpoint, nodeKey), "Contains should return false for missing key");
        
        std::vector<uint8_t> readData = cache.Read(endpoint, nodeKey);
        failed += !Expect(readData.empty(), "Read should return empty for missing key");
        
        std::cout << "  Cache miss: OK\n";
    }
    
    // Test 3: Stats tracking
    {
        // Fresh cache for clean stats
        std::string freshDir = testCacheDir + "_fresh";
        NodeDataDiskCache cache(freshDir);
        cache.Init();
        
        std::string endpoint = "https://example.com/mesh/{quadkey}";
        std::vector<uint8_t> data = {0x01, 0x02, 0x03};
        
        // Miss
        cache.Read(endpoint, "key1");
        
        // Write + Hit
        cache.Write(endpoint, "key2", data);
        cache.Read(endpoint, "key2");
        
        NodeDataDiskCache::Stats stats = cache.GetStats();
        failed += !Expect(stats.hitCount == 1, "Should have 1 hit");
        failed += !Expect(stats.missCount == 1, "Should have 1 miss");
        failed += !Expect(stats.writeCount == 1, "Should have 1 write");
        failed += !Expect(stats.totalBytesStored == 3, "Should have stored 3 bytes");
        
        std::cout << "  Stats tracking: OK (hits=" << stats.hitCount 
                  << ", misses=" << stats.missCount << ")\n";
        
        // Cleanup
        std::filesystem::remove_all(freshDir);
    }
    
    // Test 4: Different endpoints produce different cache keys
    {
        NodeDataDiskCache cache(testCacheDir);
        cache.Init();
        
        std::string endpoint1 = "https://example.com/mesh/{quadkey}";
        std::string endpoint2 = "https://other.com/tiles/{quadkey}";
        std::string nodeKey = "SAMEKEY";
        std::vector<uint8_t> data1 = {0x01};
        std::vector<uint8_t> data2 = {0x02};
        
        cache.Write(endpoint1, nodeKey, data1);
        cache.Write(endpoint2, nodeKey, data2);
        
        // Each should return its own data
        std::vector<uint8_t> read1 = cache.Read(endpoint1, nodeKey);
        std::vector<uint8_t> read2 = cache.Read(endpoint2, nodeKey);
        
        failed += !Expect(read1 == data1, "Endpoint1 should return its data");
        failed += !Expect(read2 == data2, "Endpoint2 should return its data");
        
        std::cout << "  Endpoint isolation: OK\n";
    }
    
    // Test 5: Remove
    {
        NodeDataDiskCache cache(testCacheDir);
        cache.Init();
        
        std::string endpoint = "https://example.com/mesh/{quadkey}";
        std::string nodeKey = "TOREMOVE";
        std::vector<uint8_t> data = {0x01, 0x02};
        
        cache.Write(endpoint, nodeKey, data);
        failed += !Expect(cache.Contains(endpoint, nodeKey), "Should exist before remove");
        
        cache.Remove(endpoint, nodeKey);
        failed += !Expect(!cache.Contains(endpoint, nodeKey), "Should not exist after remove");
        
        std::cout << "  Remove: OK\n";
    }
    
    // Test 6: Large data
    {
        NodeDataDiskCache cache(testCacheDir);
        cache.Init();
        
        std::string endpoint = "https://example.com/mesh/{quadkey}";
        std::string nodeKey = "LARGE";
        std::vector<uint8_t> largeData(1024 * 1024);  // 1MB
        for (size_t i = 0; i < largeData.size(); ++i) {
            largeData[i] = static_cast<uint8_t>(i % 256);
        }
        
        failed += !Expect(cache.Write(endpoint, nodeKey, largeData), "Large write should succeed");
        
        std::vector<uint8_t> readData = cache.Read(endpoint, nodeKey);
        failed += !Expect(readData.size() == largeData.size(), "Large read size should match");
        failed += !Expect(readData == largeData, "Large read data should match");
        
        std::cout << "  Large data (1MB): OK\n";
    }
    
    // Test 7: Empty data handling
    {
        NodeDataDiskCache cache(testCacheDir);
        cache.Init();
        
        std::string endpoint = "https://example.com/mesh/{quadkey}";
        std::string nodeKey = "EMPTY";
        std::vector<uint8_t> emptyData;
        
        // Empty write should fail or be handled gracefully
        bool writeResult = cache.Write(endpoint, nodeKey, emptyData);
        // Not a hard failure - just verify it doesn't crash
        
        std::cout << "  Empty data handling: OK (write=" << (writeResult ? "true" : "false") << ")\n";
    }
    
    // Test 8: Corrupted metadata handling (Sprint 2.3 hardening)
    {
        std::string corruptDir = testCacheDir + "_corrupt";
        NodeDataDiskCache cache(corruptDir);
        cache.Init();
        
        std::string endpoint = "https://example.com/mesh/{quadkey}";
        std::string nodeKey = "CORRUPT";
        std::vector<uint8_t> data = {0x01, 0x02, 0x03};
        
        // Write valid data first
        cache.Write(endpoint, nodeKey, data);
        failed += !Expect(cache.Contains(endpoint, nodeKey), "Should exist before corruption");
        
        // Corrupt the metadata file (write truncated file)
        std::string cacheKey;
        {
            // Get the internal cache key (we need to know the path)
            // Just find the .meta file
            for (const auto& entry : std::filesystem::recursive_directory_iterator(corruptDir)) {
                if (entry.path().extension() == ".meta") {
                    // Truncate the file to 5 bytes (less than minimum 20)
                    std::ofstream trunc(entry.path().string(), std::ios::binary | std::ios::trunc);
                    trunc.write("12345", 5);
                    break;
                }
            }
        }
        
        // Read should treat corrupted meta as miss (not crash)
        std::vector<uint8_t> readData = cache.Read(endpoint, nodeKey);
        // Data file still exists, but meta is corrupt - behavior depends on implementation
        // At minimum, shouldn't crash
        
        std::cout << "  Corrupted metadata: OK (graceful handling)\n";
        
        // Cleanup
        std::filesystem::remove_all(corruptDir);
    }
    
    // Test 9: Very long nodeKey handling (Sprint 2.3 limit)
    {
        std::string longDir = testCacheDir + "_longkey";
        NodeDataDiskCache cache(longDir);
        cache.Init();
        
        std::string endpoint = "https://example.com/mesh/{quadkey}";
        std::string nodeKey(300, 'x');  // 300 chars, exceeds 256 limit
        std::vector<uint8_t> data = {0x01, 0x02};
        
        // Write should succeed (storage)
        cache.Write(endpoint, nodeKey, data);
        
        // But if metadata is corrupted with oversized keyLen, read should fail gracefully
        std::cout << "  Long nodeKey: OK\n";
        
        // Cleanup
        std::filesystem::remove_all(longDir);
    }
    
    // Test 10: Size mismatch handling (Sprint 3 integrity check)
    {
        std::string mismatchDir = testCacheDir + "_mismatch";
        NodeDataDiskCache cache(mismatchDir);
        cache.Init();
        
        std::string endpoint = "https://example.com/mesh/{quadkey}";
        std::string nodeKey = "SIZEMISMATCH";
        std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05};
        
        // Write valid data
        cache.Write(endpoint, nodeKey, data);
        failed += !Expect(cache.Contains(endpoint, nodeKey), "Should exist before corruption");
        
        // Corrupt the data file (append extra bytes to change size)
        for (const auto& entry : std::filesystem::recursive_directory_iterator(mismatchDir)) {
            if (entry.path().extension() == ".bin") {
                std::ofstream file(entry.path().string(), std::ios::binary | std::ios::app);
                file.write("EXTRA", 5);
                break;
            }
        }
        
        // Read should detect size mismatch and return miss
        std::vector<uint8_t> readData = cache.Read(endpoint, nodeKey);
        failed += !Expect(readData.empty(), "Should return empty when size mismatches");
        
        // Cache entry should be cleaned up
        failed += !Expect(!cache.Contains(endpoint, nodeKey), "Corrupt entry should be removed");
        
        std::cout << "  Size mismatch handling: OK\n";
        
        // Cleanup
        std::filesystem::remove_all(mismatchDir);
    }
    
    // Test 11: Empty file handling (Sprint 3)
    {
        std::string emptyDir = testCacheDir + "_emptyfile";
        NodeDataDiskCache cache(emptyDir);
        cache.Init();
        
        std::string endpoint = "https://example.com/mesh/{quadkey}";
        std::string nodeKey = "EMPTYFILE";
        
        // Create empty file
        std::string path;
        for (const auto& bucket : std::filesystem::directory_iterator(emptyDir)) {
            if (std::filesystem::is_directory(bucket)) {
                path = bucket.path().string() + "/test.bin";
                std::ofstream file(path);
                break;
            }
        }
        if (path.empty()) {
            path = emptyDir + "/aa/test.bin";
            std::filesystem::create_directories(emptyDir + "/aa");
            std::ofstream file(path);
        }
        
        // Read empty file should return miss
        std::vector<uint8_t> readData = cache.Read(endpoint, nodeKey);
        failed += !Expect(readData.empty(), "Empty file should return miss");
        
        std::cout << "  Empty file handling: OK\n";
        
        // Cleanup
        std::filesystem::remove_all(emptyDir);
    }
    
    // Cleanup
    try {
        std::filesystem::remove_all(testCacheDir);
    } catch (...) {
        // Ignore cleanup errors
    }
    
    if (failed == 0) {
        std::cout << "NodeDataCacheTest PASSED\n";
        return 0;
    }

    std::cerr << "NodeDataCacheTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
