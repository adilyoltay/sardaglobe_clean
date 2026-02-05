#pragma once

#include <string>
#include <vector>

namespace globe {

class TileUrlTemplate {
public:
    explicit TileUrlTemplate(const std::string& templateUrl);
    std::string Build(int z, int x, int y) const;

private:
    enum class SegmentType { Literal, PlaceholderZ, PlaceholderX, PlaceholderY };
    struct Segment {
        SegmentType type;
        std::string text;
    };

    std::vector<Segment> segments_;
};

} // namespace globe
