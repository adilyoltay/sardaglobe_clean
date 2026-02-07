#pragma once

#include <cstdint>
#include <vector>

namespace globe::decoded_blob {

// Packed decoded RGBA tile blob format:
// [0..3]  : magic "NGRD"
// [4..7]  : width (uint32 LE)
// [8..11] : height (uint32 LE)
// [12..15]: dataSize bytes (uint32 LE)
// [16..]  : RGBA payload
bool Pack(int width, int height, const std::vector<uint8_t>& rgba, std::vector<uint8_t>& out);
bool Unpack(const std::vector<uint8_t>& in, int& width, int& height, std::vector<uint8_t>& rgba);
bool IsPacked(const std::vector<uint8_t>& in);

} // namespace globe::decoded_blob
