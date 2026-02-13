// Auth backoff reset behavior test.
// Verifies that consecutiveAuthFails_ resets on successful fetch.

#define private public
#include "../src/io/dem_manager.h"
#undef private

#include "../src/io/providers/terrain_rgb_provider.h"
#include "../src/debug/network_panel.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

// NetworkPanel stubs for testing
namespace globe {
NetworkPanel& NetworkPanel::Instance() {
    static NetworkPanel panel;
    return panel;
}
void NetworkPanel::RecordStart(const TileKey&, RequestType, const std::string&) {}
void NetworkPanel::RecordComplete(const TileKey&, RequestType, bool, long, size_t, double, bool, const std::string&) {}
void NetworkPanel::Render() {}
void NetworkPanel::Clear() {}
NetworkPanel::Stats NetworkPanel::GetStats() const { return Stats{}; }
NetworkRequest* NetworkPanel::FindPending(const TileKey&, RequestType) { return nullptr; }
}

using namespace globe;

// Stub provider that can simulate auth failures and successes
class TestableProvider : public ITerrainDemProvider {
public:
    int fetchCount = 0;
    int authFailCount = 0;
    int successCount = 0;
    
    bool FetchDemTile(const TileKey& key, DemGridData& outData, 
                      DemFetchResult& outResult) override {
        fetchCount++;
        
        // First 2 calls fail with auth, 3rd succeeds, 4th fails with auth
        if (fetchCount == 1 || fetchCount == 2 || fetchCount == 4) {
            authFailCount++;
            outResult = DemFetchResult::AuthError(401, "Unauthorized");
            return false;
        }
        
        // 3rd call succeeds
        successCount++;
        outData = DemGridData{};
        outData.meshN = 5;
        outData.heights.resize(25, 100.0);
        outData.valid = true;
        outResult = DemFetchResult::Success(200, 1000, 50.0);
        return true;
    }
    
    DemHealthStatus CheckHealth() override { 
        return DemHealthStatus::Healthy; 
    }
    
    DemHealthStatus GetHealthStatus() const override { 
        return DemHealthStatus::Healthy; 
    }
    
    bool IsTerminalError() const override { return false; }
    const char* GetProviderName() const override { return "testable"; }
};

int main() {
    std::cout << "=== Auth Backoff Reset Test ===" << std::endl;
    
    // Create DemManager with test provider
    DemManager::Config config;
    config.authBackoffThreshold = 3;  // Backoff after 3 consecutive auth fails
    config.authBackoffSec = 1;        // 1 second backoff for test
    config.cacheSize = 10;
    
    DemManager manager(config);
    
    // Inject test provider
    auto* testProvider = new TestableProvider();
    manager.provider_.reset(testProvider);
    
    int passed = 0;
    int failed = 0;
    
    // Test 1: Initial state - auth counter should be 0
    if (manager.consecutiveAuthFails_.load() == 0) {
        std::cout << "PASS: Initial auth counter is 0" << std::endl;
        passed++;
    } else {
        std::cout << "FAIL: Initial auth counter is " << manager.consecutiveAuthFails_.load() << std::endl;
        failed++;
    }
    
    // Test 2: First auth failure - counter should increment
    DemGridData data1;
    DemFetchResult result1;
    manager.FetchTile(TileKey(5, 10, 10), data1);
    
    if (manager.consecutiveAuthFails_.load() == 1) {
        std::cout << "PASS: Auth counter incremented to 1 after first failure" << std::endl;
        passed++;
    } else {
        std::cout << "FAIL: Auth counter is " << manager.consecutiveAuthFails_.load() << ", expected 1" << std::endl;
        failed++;
    }
    
    // Test 3: Second auth failure - counter should increment
    DemGridData data2;
    DemFetchResult result2;
    manager.FetchTile(TileKey(5, 11, 11), data2);
    
    if (manager.consecutiveAuthFails_.load() == 2) {
        std::cout << "PASS: Auth counter incremented to 2 after second failure" << std::endl;
        passed++;
    } else {
        std::cout << "FAIL: Auth counter is " << manager.consecutiveAuthFails_.load() << ", expected 2" << std::endl;
        failed++;
    }
    
    // Test 4: Successful fetch - counter should reset to 0
    DemGridData data3;
    DemFetchResult result3;
    bool success = manager.FetchTile(TileKey(5, 12, 12), data3);
    
    if (success && manager.consecutiveAuthFails_.load() == 0) {
        std::cout << "PASS: Auth counter reset to 0 after successful fetch" << std::endl;
        passed++;
    } else {
        std::cout << "FAIL: Auth counter is " << manager.consecutiveAuthFails_.load() 
                  << " (success=" << success << "), expected 0" << std::endl;
        failed++;
    }
    
    // Test 5: Auth failure after success - counter should start from 0
    DemGridData data4;
    DemFetchResult result4;
    manager.FetchTile(TileKey(5, 13, 13), data4);
    
    if (manager.consecutiveAuthFails_.load() == 1) {
        std::cout << "PASS: Auth counter correctly at 1 after post-success failure" << std::endl;
        passed++;
    } else {
        std::cout << "FAIL: Auth counter is " << manager.consecutiveAuthFails_.load() << ", expected 1" << std::endl;
        failed++;
    }
    
    manager.Shutdown();
    
    std::cout << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;
    
    return failed > 0 ? 1 : 0;
}
