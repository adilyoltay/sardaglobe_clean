#include "decoded_tile_blob.h"
#include <limits>
#include <cstring>
#include <cstddef>

namespace globe::decoded_blob {

namespace {

constexpr uint8_t kMagic[4] = {'N', 'G', 'R', 'D'};
constexpr size_t kHeaderSize = 16;

void WriteU32LE(std::vector<uint8_t>& out, size_t offset, uint32_t value) {
    out[offset + 0] = static_cast<uint8_t>(value & 0xFFu);
    out[offset + 1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
    out[offset + 2] = static_cast<uint8_t>((value >> 16u) & 0xFFu);
    out[offset + 3] = static_cast<uint8_t>((value >> 24u) & 0xFFu);
}

uint32_t ReadU32LE(const std::vector<uint8_t>& in, size_t offset) {
    return static_cast<uint32_t>(in[offset + 0]) |
           (static_cast<uint32_t>(in[offset + 1]) << 8u) |
           (static_cast<uint32_t>(in[offset + 2]) << 16u) |
           (static_cast<uint32_t>(in[offset + 3]) << 24u);
}

} // namespace

bool IsPacked(const std::vector<uint8_t>& in) {
    return in.size() >= kHeaderSize && std::memcmp(in.data(), kMagic, sizeof(kMagic)) == 0;
}

bool Pack(int width, int height, const std::vector<uint8_t>& rgba, std::vector<uint8_t>& out) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    uint64_t expectedSize64 = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull;
    if (expectedSize64 > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    uint32_t expectedSize = static_cast<uint32_t>(expectedSize64);
    if (rgba.size() != expectedSize) {
        return false;
    }

    out.assign(kHeaderSize + rgba.size(), 0);
    std::memcpy(out.data(), kMagic, sizeof(kMagic));
    WriteU32LE(out, 4, static_cast<uint32_t>(width));
    WriteU32LE(out, 8, static_cast<uint32_t>(height));
    WriteU32LE(out, 12, static_cast<uint32_t>(rgba.size()));
    std::memcpy(out.data() + kHeaderSize, rgba.data(), rgba.size());
    return true;
}

bool Unpack(const std::vector<uint8_t>& in, int& width, int& height, std::vector<uint8_t>& rgba) {
    if (!IsPacked(in)) {
        return false;
    }

    uint32_t w = ReadU32LE(in, 4);
    uint32_t h = ReadU32LE(in, 8);
    uint32_t payloadSize = ReadU32LE(in, 12);

    if (w == 0 || h == 0) {
        return false;
    }

    uint64_t expectedSize64 = static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * 4ull;
    if (expectedSize64 > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    uint32_t expectedSize = static_cast<uint32_t>(expectedSize64);
    if (payloadSize != expectedSize) {
        return false;
    }

    if (in.size() != kHeaderSize + static_cast<size_t>(payloadSize)) {
        return false;
    }

    width = static_cast<int>(w);
    height = static_cast<int>(h);
    rgba.assign(in.begin() + static_cast<std::ptrdiff_t>(kHeaderSize), in.end());
    return true;
}

} // namespace globe::decoded_blob
