// Bayer Matrix Dithering Test
// Validates Fix 3: Stochastic crossfade with Bayer matrix

#include <iostream>
#include <cmath>
#include <vector>

using namespace std;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        return false;
    }
    return true;
}

void Report(const char* test) {
    std::cerr << "PASSED: " << test << std::endl;
}

// 8x8 Bayer matrix (same as in shader)
const float bayer8x8[64] = {
    0.0f/64.0f, 48.0f/64.0f, 12.0f/64.0f, 60.0f/64.0f, 3.0f/64.0f, 51.0f/64.0f, 15.0f/64.0f, 63.0f/64.0f,
    32.0f/64.0f, 16.0f/64.0f, 44.0f/64.0f, 28.0f/64.0f, 35.0f/64.0f, 19.0f/64.0f, 47.0f/64.0f, 31.0f/64.0f,
    8.0f/64.0f, 56.0f/64.0f, 4.0f/64.0f, 52.0f/64.0f, 11.0f/64.0f, 59.0f/64.0f, 7.0f/64.0f, 55.0f/64.0f,
    40.0f/64.0f, 24.0f/64.0f, 36.0f/64.0f, 20.0f/64.0f, 43.0f/64.0f, 27.0f/64.0f, 39.0f/64.0f, 23.0f/64.0f,
    2.0f/64.0f, 50.0f/64.0f, 14.0f/64.0f, 62.0f/64.0f, 1.0f/64.0f, 49.0f/64.0f, 13.0f/64.0f, 61.0f/64.0f,
    34.0f/64.0f, 18.0f/64.0f, 46.0f/64.0f, 30.0f/64.0f, 33.0f/64.0f, 17.0f/64.0f, 45.0f/64.0f, 29.0f/64.0f,
    10.0f/64.0f, 58.0f/64.0f, 6.0f/64.0f, 54.0f/64.0f, 9.0f/64.0f, 57.0f/64.0f, 5.0f/64.0f, 53.0f/64.0f,
    42.0f/64.0f, 26.0f/64.0f, 38.0f/64.0f, 22.0f/64.0f, 41.0f/64.0f, 25.0f/64.0f, 37.0f/64.0f, 21.0f/64.0f
};

float GetBayerValue(int x, int y) {
    int px = x % 8;
    int py = y % 8;
    return bayer8x8[py * 8 + px];
}

// Simulate shader dither logic
float DitheredBlend(float blend, int screenX, int screenY) {
    float bayer = GetBayerValue(screenX, screenY);
    return blend + (bayer - 0.5f) * 0.25f;
}

} // namespace

int main() {
    int failed = 0;
    
    // Test 1: Bayer matrix values are in valid range [0, 1)
    {
        bool allValid = true;
        for (int i = 0; i < 64; ++i) {
            if (bayer8x8[i] < 0.0f || bayer8x8[i] >= 1.0f) {
                allValid = false;
                break;
            }
        }
        failed += !Expect(allValid, "All Bayer values should be in [0, 1)");
        
        if (failed == 0) Report("BayerValueRange");
    }
    
    // Test 2: Bayer matrix has good distribution (no clumping)
    {
        float minVal = 1.0f;
        float maxVal = 0.0f;
        float sum = 0.0f;
        
        for (int i = 0; i < 64; ++i) {
            minVal = std::min(minVal, bayer8x8[i]);
            maxVal = std::max(maxVal, bayer8x8[i]);
            sum += bayer8x8[i];
        }
        
        float avg = sum / 64.0f;
        
        failed += !Expect(minVal < 0.1f, "Min value should be close to 0");
        failed += !Expect(maxVal > 0.9f, "Max value should be close to 1");
        failed += !Expect(std::abs(avg - 0.5f) < 0.1f, "Average should be close to 0.5");
        
        if (failed == 0) Report("BayerDistribution");
    }
    
    // Test 3: Dithering creates stochastic effect
    {
        float blend = 0.5f;
        int useChildCount = 0;
        const int samples = 64; // Sample all Bayer values
        
        for (int i = 0; i < samples; ++i) {
            int x = i % 8;
            int y = i / 8;
            float dithered = DitheredBlend(blend, x, y);
            float useChild = (dithered <= 0.5f) ? 1.0f : 0.0f;
            useChildCount += (useChild > 0.5f) ? 1 : 0;
        }
        
        // At 50% blend, roughly half should use child (with dithering)
        // Without dithering, all would use child (blend 0.5 <= 0.5)
        bool hasVariation = useChildCount > 20 && useChildCount < 44;
        
        failed += !Expect(hasVariation, 
            "Dithering should create variation at 50% blend");
        
        if (failed == 0) Report("DitherStochasticEffect");
    }
    
    // Test 4: Dithering is effective in mid-range blends
    {
        // Dithering is most effective in the 0.25-0.75 range
        // At 0.5 blend with dithering range [-0.125, +0.125], we get:
        // dithered in [0.375, 0.625], crossing 0.5 threshold
        float blend = 0.5f;
        int childCount = 0;
        const int samples = 64;
        
        for (int i = 0; i < samples; ++i) {
            int x = i % 8;
            int y = i / 8;
            float dithered = DitheredBlend(blend, x, y);
            if (dithered <= 0.5f) {
                childCount++;
            }
        }
        
        // At 0.5 blend with dithering, we should get roughly 50/50
        // But with noise, expect 30-70 range
        bool ratioPreserved = childCount >= 20 && childCount <= 44;
        
        std::string msg = "Dithering should create variation at 50% blend (got " + 
                         std::to_string(childCount) + "/64)";
        failed += !Expect(ratioPreserved, msg.c_str());
        
        if (failed == 0) Report("DitherMidRange");
    }
    
    // Test 5: Dithering range verification
    {
        // At blend 0.0: dithered = -0.125 to +0.125 (all <= 0.5, so all child)
        // At blend 1.0: dithered = 0.875 to 1.125 (all > 0.5, so all parent)
        // At blend 0.5: dithered = 0.375 to 0.625 (mix of child/parent)
        
        // Verify the mathematical ranges
        float minBayer = 1.0f, maxBayer = 0.0f;
        for (int i = 0; i < 64; ++i) {
            minBayer = std::min(minBayer, bayer8x8[i]);
            maxBayer = std::max(maxBayer, bayer8x8[i]);
        }
        
        float ditherMin = (minBayer - 0.5f) * 0.25f;
        float ditherMax = (maxBayer - 0.5f) * 0.25f;
        
        // At blend 0, all should use child (dithered <= 0.5)
        bool allChildAtZero = (0.0f + ditherMax) <= 0.5f;
        failed += !Expect(allChildAtZero, "At blend 0.0, all should use child (dither range check)");
        
        // At blend 1, all should use parent (dithered > 0.5)
        bool allParentAtOne = (1.0f + ditherMin) > 0.5f;
        failed += !Expect(allParentAtOne, "At blend 1.0, all should use parent (dither range check)");
        
        if (failed == 0) Report("DitherRangeVerification");
    }
    
    if (failed == 0) {
        std::cout << "bayer_matrix_dithering_test: ALL PASSED" << std::endl;
    } else {
        std::cout << "bayer_matrix_dithering_test: " << failed << " FAILED" << std::endl;
    }
    
    return failed;
}
