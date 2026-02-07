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
    }

    failed += !Expect(decoder.GetDecodedBlobHits() >= 1, "decoded blob fastpath counter should increment");
    decoder.Shutdown();

    if (failed == 0) {
        std::cout << "TileDecoderBlobFastpathTest PASSED\n";
        return 0;
    }

    std::cerr << "TileDecoderBlobFastpathTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
