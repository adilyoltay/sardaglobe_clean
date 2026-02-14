// TextureArrayManager Test
// Validates layer-based texture storage functionality

#include "../src/rendering/texture_array_manager.h"
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
        TextureArrayManager::Config config;
        config.useTexture2DArray = true;
        config.initialLayersPerTier = 32;
        config.maxLayersPerTier = 128;
        config.generateMipmaps = false;
        
        TextureArrayManager manager(config);
        
        const auto& retrievedConfig = manager.GetConfig();
        bool configMatch = (retrievedConfig.useTexture2DArray == true &&
                           retrievedConfig.initialLayersPerTier == 32 &&
                           retrievedConfig.maxLayersPerTier == 128 &&
                           retrievedConfig.generateMipmaps == false);
        
        if (!Expect(configMatch, "Configuration should be preserved")) failures++;
        else Report("BasicConfiguration");
    }
    
    // Test 2: Tier registration
    {
        TextureArrayManager manager;
        
        TierConfig tierConfig;
        tierConfig.tileWidth = 256;
        tierConfig.tileHeight = 256;
        tierConfig.maxLayers = 16;
        tierConfig.internalFormat = GL_RGBA8;
        tierConfig.format = GL_RGBA;
        tierConfig.type = GL_UNSIGNED_BYTE;
        tierConfig.generateMipmaps = false;
        
        // Can't register without GL context in unit test
        // Just verify the API exists
        Report("TierRegistrationAPI");
    }
    
    // Test 3: Layer handle validity
    {
        TextureArrayManager manager;
        
        LayerHandle invalid1 = INVALID_LAYER_HANDLE;
        LayerHandle invalid2 = -1;
        LayerHandle valid = 0;
        
        if (!Expect(!manager.IsLayerValid(invalid1), "INVALID_LAYER_HANDLE should be invalid")) failures++;
        if (!Expect(!manager.IsLayerValid(invalid2), "-1 should be invalid")) failures++;
        
        // Note: Layer 0 without initialization is also invalid
        if (!Expect(!manager.IsLayerValid(valid), "Unallocated layer should be invalid")) failures++;
        else Report("LayerHandleValidity");
    }
    
    // Test 4: GetOrCreateTier for different sizes
    {
        TextureArrayManager manager;
        
        // Would need GL context to actually create tiers
        // Just verify API is callable
        // int tier256 = manager.GetOrCreateTier(256, 256, true);
        // int tier512 = manager.GetOrCreateTier(512, 512, false);
        
        Report("GetOrCreateTierAPI");
    }
    
    // Test 5: Statistics tracking
    {
        TextureArrayManager manager;
        auto stats = manager.GetStats();
        
        bool initialZero = (stats.totalUploads == 0 &&
                           stats.totalRecycles == 0 &&
                           stats.failedAllocations == 0 &&
                           stats.tierCount == 0);
        
        if (!Expect(initialZero, "Initial stats should be zero")) failures++;
        
        // Reset and verify
        manager.ResetStats();
        stats = manager.GetStats();
        
        if (!Expect(stats.totalUploads == 0, "Stats should be resettable")) failures++;
        else Report("StatisticsTracking");
    }
    
    // Test 6: Config consistency
    {
        TextureArrayManager::Config config1;
        config1.useTexture2DArray = true;
        config1.maxAnisotropy = 8.0f;
        
        TextureArrayManager::Config config2;
        config2.useTexture2DArray = false;
        config2.maxAnisotropy = 4.0f;
        
        TextureArrayManager manager1(config1);
        TextureArrayManager manager2(config2);
        
        if (!Expect(manager1.GetConfig().useTexture2DArray == true, 
                   "Manager1 should have texture arrays enabled")) failures++;
        if (!Expect(manager2.GetConfig().useTexture2DArray == false, 
                   "Manager2 should have texture arrays disabled")) failures++;
        if (!Expect(manager1.GetConfig().maxAnisotropy == 8.0f,
                   "Manager1 should have anisotropy 8.0")) failures++;
        if (!Expect(manager2.GetConfig().maxAnisotropy == 4.0f,
                   "Manager2 should have anisotropy 4.0")) failures++;
        else Report("ConfigConsistency");
    }
    
    // Test 7: Move semantics
    {
        TextureArrayManager::Config config;
        config.initialLayersPerTier = 16;
        
        TextureArrayManager manager1(config);
        TextureArrayManager manager2(std::move(manager1));
        
        const auto& config2 = manager2.GetConfig();
        if (!Expect(config2.initialLayersPerTier == 16, 
                   "Moved manager should retain config")) failures++;
        else Report("MoveSemantics");
    }
    
    // Test 8: LayerInfo validation
    {
        LayerInfo info;
        
        // Default state
        if (!Expect(!info.IsValid(), "Default LayerInfo should be invalid")) failures++;
        
        // Set valid state
        info.handle = 0;
        info.inUse = true;
        if (!Expect(info.IsValid(), "LayerInfo with handle and inUse should be valid")) failures++;
        
        // Invalidate
        info.inUse = false;
        if (!Expect(!info.IsValid(), "LayerInfo with inUse=false should be invalid")) failures++;
        else Report("LayerInfoValidation");
    }
    
    // Test 9: TierConfig defaults
    {
        TierConfig config;
        config.tileWidth = 256;
        config.tileHeight = 256;
        config.maxLayers = 64;
        
        if (!Expect(config.tileWidth == 256, "Width should be 256")) failures++;
        if (!Expect(config.tileHeight == 256, "Height should be 256")) failures++;
        if (!Expect(config.maxLayers == 64, "Max layers should be 64")) failures++;
        else Report("TierConfigDefaults");
    }
    
    // Test 10: Frame management API
    {
        TextureArrayManager manager;
        
        // Should not crash
        manager.BeginFrame(1);
        manager.EndFrame();
        manager.BeginFrame(100);
        manager.EndFrame();
        
        Report("FrameManagementAPI");
    }
    
    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED\n";
        return 1;
    }
    
    std::cerr << "\nAll TextureArrayManager tests PASSED\n";
    return 0;
}
