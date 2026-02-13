// Tile Decoder Blob Fastpath Test
// Verifies decoded blob input bypasses image codec and returns RGBA directly.

#include "../src/io/tile_decoder.h"
#include "../src/io/decoded_tile_blob.h"
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <mutex>

using namespace globe;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    int failed = 0;

    const int width = 2;
    const int height = 2;
    const std::vector<uint8_t> rgba = {
        10, 20, 30, 255,
        40, 50, 60, 255,
        70, 80, 90, 255,
        100, 110, 120, 255
    };

    std::vector<uint8_t> packed;
    failed += !Expect(decoded_blob::Pack(width, height, rgba, packed), "decoded blob pack should succeed");
    if (failed != 0) {
        return 1;
    }

    TileDecoder decoder(1);
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    DecodeResult result;

    decoder.SetResultCallback([&](DecodeResult r) {
        std::lock_guard<std::mutex> lock(mutex);
        result = std::move(r);
        done = true;
        cv.notify_one();
    });

    DecodeRequest req;
    req.key = TileKey(4, 3, 2);
    req.data = packed;
    req.priority = Priority::Normal;
    req.score = 1.0;
    decoder.Decode(std::move(req));

    {
        std::unique_lock<std::mutex> lock(mutex);
        bool signaled = cv.wait_for(lock, std::chrono::seconds(2), [&]() { return done; });
        failed += !Expect(signaled, "decoder callback should be signaled");
    }

    if (done) {
        failed += !Expect(result.success, "decode result should be successful");
        failed += !Expect(result.width == width && result.height == height, "decoded dimensions should match");
        failed += !Expect(result.pixels == rgba, "decoded pixels should match source");
        failed += !Expect(!result.hasTransparency, "opaque source should not be flagged transparent");
        failed += !Expect(!result.mostlyBlackOpaque, "color source should not be flagged mostly black opaque");
    }

    // Second decode: fully opaque black tile should trigger nodata-black heuristic.
    done = false;
    DecodeResult blackResult;
    const int blackW = 32;
    const int blackH = 32;
    std::vector<uint8_t> blackRgba(static_cast<size_t>(blackW * blackH * 4), 0);
    for (size_t i = 3; i < blackRgba.size(); i += 4) {
        blackRgba[i] = 255;
    }
    std::vector<uint8_t> blackPacked;
    failed += !Expect(decoded_blob::Pack(blackW, blackH, blackRgba, blackPacked),
                      "black decoded blob pack should succeed");
    DecodeRequest blackReq;
    blackReq.key = TileKey(4, 4, 2);
    blackReq.data = std::move(blackPacked);
    blackReq.priority = Priority::Normal;
    blackReq.score = 1.0;
    decoder.SetResultCallback([&](DecodeResult r) {
        std::lock_guard<std::mutex> lock(mutex);
        blackResult = std::move(r);
        done = true;
        cv.notify_one();
    });
    decoder.Decode(std::move(blackReq));
    {
        std::unique_lock<std::mutex> lock(mutex);
        bool signaled = cv.wait_for(lock, std::chrono::seconds(2), [&]() { return done; });
        failed += !Expect(signaled, "decoder callback should be signaled for black tile");
    }
    if (done) {
        failed += !Expect(blackResult.success, "black decode result should be successful");
        failed += !Expect(!blackResult.hasTransparency, "opaque black tile should not be flagged transparent");
        failed += !Expect(blackResult.mostlyBlackOpaque, "opaque black tile should be flagged mostly black");
    }

    failed += !Expect(decoder.GetDecodedBlobHits() >= 1, "decoded blob fastpath counter should increment");

    // =========================================================================
    // #5: Black nodata detector false-positive tests (variance guard)
    // =========================================================================

    // Test 3: Dark textured tile (gradient/noise) - should NOT be flagged as black nodata
    // This tests that "dark but detailed" imagery doesn't trigger false-positive
    done = false;
    DecodeResult darkTexturedResult;
    const int texturedW = 32;
    const int texturedH = 32;
    std::vector<uint8_t> texturedRgba(static_cast<size_t>(texturedW * texturedH * 4), 0);
    // Create a dark gradient with variation (simulates real dark terrain imagery)
    for (int y = 0; y < texturedH; ++y) {
        for (int x = 0; x < texturedW; ++x) {
            size_t idx = static_cast<size_t>((y * texturedW + x) * 4);
            // Gradient from 0-20 with noise (variance ensures it's not nodata)
            uint8_t value = static_cast<uint8_t>((x + y) % 21);  // 0-20 range
            texturedRgba[idx + 0] = value;
            texturedRgba[idx + 1] = value;
            texturedRgba[idx + 2] = static_cast<uint8_t>((value + 5) % 21);  // Slight variation
            texturedRgba[idx + 3] = 255;  // Opaque
        }
    }
    std::vector<uint8_t> texturedPacked;
    failed += !Expect(decoded_blob::Pack(texturedW, texturedH, texturedRgba, texturedPacked),
                      "dark textured decoded blob pack should succeed");
    DecodeRequest texturedReq;
    texturedReq.key = TileKey(4, 5, 2);
    texturedReq.data = std::move(texturedPacked);
    texturedReq.priority = Priority::Normal;
    texturedReq.score = 1.0;
    decoder.SetResultCallback([&](DecodeResult r) {
        std::lock_guard<std::mutex> lock(mutex);
        darkTexturedResult = std::move(r);
        done = true;
        cv.notify_one();
    });
    decoder.Decode(std::move(texturedReq));
    {
        std::unique_lock<std::mutex> lock(mutex);
        bool signaled = cv.wait_for(lock, std::chrono::seconds(2), [&]() { return done; });
        failed += !Expect(signaled, "decoder callback should be signaled for dark textured tile");
    }
    if (done) {
        failed += !Expect(darkTexturedResult.success, "dark textured decode result should be successful");
        // KEY ASSERTION: Dark textured tile should NOT be flagged as black nodata
        // (variance guard prevents false-positive)
        failed += !Expect(!darkTexturedResult.mostlyBlackOpaque,
                          "dark textured tile (with variance) should NOT be flagged mostly black");
    }

    // Test 4: Black tile with small artefact - should still be flagged as nodata
    // This tests that small non-black regions don't prevent nodata detection
    done = false;
    DecodeResult blackWithArtefactResult;
    const int artefactW = 32;
    const int artefactH = 32;
    std::vector<uint8_t> artefactRgba(static_cast<size_t>(artefactW * artefactH * 4), 0);
    // 98% black pixels, 2% with value 3 (small artefact)
    for (int i = 3; i < artefactW * artefactH * 4; i += 4) {
        artefactRgba[i] = 255;  // Opaque
    }
    // Add small artefacts (2% of pixels)
    for (int i = 0; i < (artefactW * artefactH) / 50; ++i) {
        size_t idx = static_cast<size_t>(i * 4 * 10);  // Every 10th pixel
        if (idx + 2 < artefactRgba.size()) {
            artefactRgba[idx + 0] = 3;
            artefactRgba[idx + 1] = 3;
            artefactRgba[idx + 2] = 3;
        }
    }
    std::vector<uint8_t> artefactPacked;
    failed += !Expect(decoded_blob::Pack(artefactW, artefactH, artefactRgba, artefactPacked),
                      "black with artefact decoded blob pack should succeed");
    DecodeRequest artefactReq;
    artefactReq.key = TileKey(4, 6, 2);
    artefactReq.data = std::move(artefactPacked);
    artefactReq.priority = Priority::Normal;
    artefactReq.score = 1.0;
    decoder.SetResultCallback([&](DecodeResult r) {
        std::lock_guard<std::mutex> lock(mutex);
        blackWithArtefactResult = std::move(r);
        done = true;
        cv.notify_one();
    });
    decoder.Decode(std::move(artefactReq));
    {
        std::unique_lock<std::mutex> lock(mutex);
        bool signaled = cv.wait_for(lock, std::chrono::seconds(2), [&]() { return done; });
        failed += !Expect(signaled, "decoder callback should be signaled for black+artefact tile");
    }
    if (done) {
        failed += !Expect(blackWithArtefactResult.success, "black+artefact decode result should be successful");
        // KEY ASSERTION: Black tile with small artefacts should still be flagged as nodata
        failed += !Expect(blackWithArtefactResult.mostlyBlackOpaque,
                          "black tile with small artefacts should still be flagged mostly black");
    }

    decoder.Shutdown();

    if (failed == 0) {
        std::cout << "TileDecoderBlobFastpathTest PASSED\n";
        return 0;
    }

    std::cerr << "TileDecoderBlobFastpathTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
