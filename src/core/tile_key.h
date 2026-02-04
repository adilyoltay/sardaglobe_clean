#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <functional>

namespace globe {

// Immutable tile identifier using quadkey addressing
struct TileKey {
    int level = 0;  // Zoom level (0-22)
    int x = 0;      // Column
    int y = 0;      // Row

    constexpr TileKey() = default;
    constexpr TileKey(int z, int tx, int ty) : level(z), x(tx), y(ty) {}

    // Comparison
    constexpr bool operator==(const TileKey& other) const {
        return level == other.level && x == other.x && y == other.y;
    }
    constexpr bool operator!=(const TileKey& other) const {
        return !(*this == other);
    }
    constexpr bool operator<(const TileKey& other) const {
        if (level != other.level) return level < other.level;
        if (y != other.y) return y < other.y;
        return x < other.x;
    }

    // Validity check
    constexpr bool IsValid() const {
        if (level < 0 || level > 22) return false;
        int maxCoord = (1 << level) - 1;
        return x >= 0 && x <= maxCoord && y >= 0 && y <= maxCoord;
    }

    // Parent tile (one level up)
    constexpr TileKey Parent() const {
        if (level <= 0) return *this;
        return TileKey(level - 1, x >> 1, y >> 1);
    }

    // Four children (one level down)
    std::array<TileKey, 4> Children() const {
        int childLevel = level + 1;
        int childX = x << 1;
        int childY = y << 1;
        return {{
            {childLevel, childX,     childY},
            {childLevel, childX + 1, childY},
            {childLevel, childX,     childY + 1},
            {childLevel, childX + 1, childY + 1}
        }};
    }

    // Neighbor tiles (with wrap-around for X)
    TileKey Neighbor(int dx, int dy) const {
        int n = 1 << level;
        int nx = (x + dx + n) % n;  // Wrap X
        int ny = y + dy;
        if (ny < 0 || ny >= n) return TileKey(-1, -1, -1);  // Invalid
        return TileKey(level, nx, ny);
    }
    
    // All 8 neighbors (for prefetch)
    std::array<TileKey, 8> Neighbors() const {
        return {{
            Neighbor(-1, -1), Neighbor(0, -1), Neighbor(1, -1),
            Neighbor(-1,  0),                  Neighbor(1,  0),
            Neighbor(-1,  1), Neighbor(0,  1), Neighbor(1,  1)
        }};
    }

    // String conversion "z/x/y"
    std::string ToString() const {
        return std::to_string(level) + "/" + std::to_string(x) + "/" + std::to_string(y);
    }

    static TileKey FromString(const std::string& s) {
        TileKey key;
        if (std::sscanf(s.c_str(), "%d/%d/%d", &key.level, &key.x, &key.y) != 3) {
            return TileKey(-1, -1, -1);
        }
        return key;
    }

    // Hash for use in unordered containers
    struct Hash {
        std::size_t operator()(const TileKey& k) const noexcept {
            // Pack into 64-bit: level(5) | y(29) | x(30)
            uint64_t packed = (static_cast<uint64_t>(k.level) << 59) |
                              (static_cast<uint64_t>(k.y) << 30) |
                              static_cast<uint64_t>(k.x);
            return std::hash<uint64_t>{}(packed);
        }
    };
};

} // namespace globe

// std::hash specialization
namespace std {
template<>
struct hash<globe::TileKey> {
    std::size_t operator()(const globe::TileKey& k) const noexcept {
        return globe::TileKey::Hash{}(k);
    }
};
}
