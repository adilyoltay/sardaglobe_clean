// Decoded Tile Blob Test
// Verifies pack/unpack integrity and corruption checks.

#include "../src/io/decoded_tile_blob.h"
#include <iostream>
#include <vector>

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
    std::vector<uint8_t> rgba = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 255, 255
    };

    std::vector<uint8_t> packed;
    failed += !Expect(decoded_blob::Pack(width, height, rgba, packed), "pack should succeed");
    failed += !Expect(decoded_blob::IsPacked(packed), "packed blob magic should be recognized");

    int outW = 0;
    int outH = 0;
    std::vector<uint8_t> outRgba;
    failed += !Expect(decoded_blob::Unpack(packed, outW, outH, outRgba), "unpack should succeed");
    failed += !Expect(outW == width && outH == height, "dimensions should round-trip");
    failed += !Expect(outRgba == rgba, "rgba payload should round-trip");

    // Corrupt payload size field -> unpack should fail.
    if (packed.size() >= 16) {
        packed[12] ^= 0x01;
    }
    outRgba.clear();
    failed += !Expect(!decoded_blob::Unpack(packed, outW, outH, outRgba), "corrupt blob should fail unpack");

    if (failed == 0) {
        std::cout << "DecodedTileBlobTest PASSED\n";
        return 0;
    }

    std::cerr << "DecodedTileBlobTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
