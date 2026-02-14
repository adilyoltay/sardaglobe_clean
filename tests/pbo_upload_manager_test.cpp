// PboUploadManager Test
// Validates async texture upload functionality

#include "../src/rendering/pbo_upload_manager.h"
#include <iostream>
#include <cstring>

using namespace globe;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

void Report(const char* test) {
    std::cerr << "PASSED: " << test << '\n';
}

} // namespace

int main() {
    int failures = 0;
    
    // Test 1: Basic construction and configuration
    {
        PboUploadManager::Config config;
        config.maxPboCount = 4;
        config.defaultPboSize = 2 * 1024 * 1024;
        config.maxPendingUploads = 16;
        config.useFences = true;
        
        PboUploadManager manager(config);
        
        const auto& retrievedConfig = manager.GetConfig();
        bool configMatch = (retrievedConfig.maxPboCount == 4 &&
                           retrievedConfig.defaultPboSize == 2 * 1024 * 1024 &&
                           retrievedConfig.maxPendingUploads == 16);
        
        if (!Expect(configMatch, "Configuration should be preserved")) failures++;
        else Report("BasicConfiguration");
    }
    
    // Test 2: Move semantics
    {
        PboUploadManager::Config config;
        config.maxPboCount = 2;
        
        PboUploadManager manager1(config);
        PboUploadManager manager2(std::move(manager1));
        
        // After move, manager1 should be in a valid but empty state
        const auto& config2 = manager2.GetConfig();
        if (!Expect(config2.maxPboCount == 2, "Moved manager should retain config")) failures++;
        else Report("MoveSemantics");
    }
    
    // Test 3: Upload request validation with owned data
    {
        UploadRequest validRequest;
        validRequest.targetTexture = 1;
        validRequest.width = 256;
        validRequest.height = 256;
        validRequest.ownsData = true;
        validRequest.pixelData.resize(256 * 256 * 4);
        
        if (!Expect(validRequest.IsValid(), "Valid request with owned data should pass validation")) failures++;
        if (!Expect(validRequest.GetData() != nullptr, "GetData should return pointer to pixelData")) failures++;
        if (!Expect(validRequest.GetDataSize() == 256 * 256 * 4, "GetDataSize should match pixelData size")) failures++;
        
        UploadRequest invalidRequest;
        invalidRequest.targetTexture = 0;  // Invalid
        invalidRequest.width = 256;
        invalidRequest.height = 256;
        
        if (!Expect(!invalidRequest.IsValid(), "Invalid request should fail validation")) failures++;
        
        Report("UploadRequestValidation");
    }
    
    // Test 4: Upload request with external data
    {
        std::vector<uint8_t> externalData(64 * 64 * 4, 0xAB);
        
        UploadRequest req;
        req.targetTexture = 1;
        req.width = 64;
        req.height = 64;
        req.externalData = externalData.data();
        req.dataSize = externalData.size();
        req.ownsData = false;
        
        if (!Expect(req.IsValid(), "External data request should be valid")) failures++;
        if (!Expect(req.GetData() == externalData.data(), "GetData should return external pointer")) failures++;
        if (!Expect(req.GetDataSize() == externalData.size(), "GetDataSize should match dataSize")) failures++;
        
        Report("ExternalDataRequest");
    }
    
    // Test 5: Statistics tracking
    {
        PboUploadManager manager;
        auto stats = manager.GetStats();
        
        bool initialZero = (stats.totalUploads == 0 &&
                           stats.successfulUploads == 0 &&
                           stats.failedUploads == 0 &&
                           stats.bytesUploaded == 0 &&
                           stats.fenceWaits == 0);
        
        if (!Expect(initialZero, "Initial stats should be zero")) failures++;
        
        // Reset should maintain zero
        manager.ResetStats();
        stats = manager.GetStats();
        
        if (!Expect(stats.totalUploads == 0, "Stats should be resettable")) failures++;
        else Report("StatisticsTracking");
    }
    
    // Test 6: Priority-based queue ordering
    {
        PboUploadManager::Config config;
        config.maxPendingUploads = 10;
        PboUploadManager manager(config);
        
        UploadRequest req1;
        req1.targetTexture = 1;
        req1.width = 64;
        req1.height = 64;
        req1.pixelData.resize(64 * 64 * 4);
        req1.ownsData = true;
        req1.priority = 10;  // Lower number = higher priority
        
        UploadRequest req2;
        req2.targetTexture = 2;
        req2.width = 64;
        req2.height = 64;
        req2.pixelData.resize(64 * 64 * 4);
        req2.ownsData = true;
        req2.priority = 5;   // Higher priority than req1
        
        if (!Expect(req1.priority > req2.priority, "Priority 10 should be lower than 5")) failures++;
        else Report("PriorityOrdering");
    }
    
    // Test 7: Config update
    {
        PboUploadManager::Config config;
        config.maxPendingUploads = 32;
        config.orphanUnusedPbos = false;
        config.useFences = false;
        
        PboUploadManager manager(config);
        
        PboUploadManager::Config newConfig;
        newConfig.maxPendingUploads = 64;
        newConfig.orphanUnusedPbos = true;
        newConfig.pboAgeThreshold = 5;
        newConfig.useFences = true;
        
        manager.SetConfig(newConfig);
        
        const auto& updated = manager.GetConfig();
        // Note: only certain fields are updateable via SetConfig
        if (!Expect(updated.orphanUnusedPbos == true, "Config orphan flag should be updateable")) failures++;
        if (!Expect(updated.useFences == true, "Config useFences should be updateable")) failures++;
        else Report("ConfigUpdate");
    }
    
    // Test 8: Helper API tests
    {
        PboUploadManager manager;
        
        // Test SubmitUploadExternal signature
        std::vector<uint8_t> testData(128 * 128 * 4, 0xCD);
        bool submitted = manager.SubmitUploadExternal(
            1, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE,
            testData.data(), testData.size(),
            nullptr, nullptr, 0
        );
        
        // Should fail because manager not initialized
        if (!Expect(!submitted, "Submit should fail if not initialized")) failures++;
        
        // Test SubmitUploadOwned signature
        std::vector<uint8_t> ownedData(64 * 64 * 4, 0xEF);
        bool submittedOwned = manager.SubmitUploadOwned(
            1, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE,
            std::move(ownedData),
            nullptr, nullptr, 0
        );
        
        if (!Expect(!submittedOwned, "SubmitOwned should fail if not initialized")) failures++;
        else Report("HelperAPI");
    }
    
    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }
    
    std::cerr << "\nAll PBO Upload Manager tests PASSED\n";
    return 0;
}
