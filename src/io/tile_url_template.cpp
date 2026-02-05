#include "tile_url_template.h"

namespace globe {

TileUrlTemplate::TileUrlTemplate(const std::string& templateUrl) {
    std::string literal;
    size_t i = 0;
    while (i < templateUrl.size()) {
        if (templateUrl[i] == '{' && i + 2 < templateUrl.size() && templateUrl[i + 2] == '}') {
            char token = templateUrl[i + 1];
            SegmentType type;
            if (token == 'z') {
                type = SegmentType::PlaceholderZ;
            } else if (token == 'x') {
                type = SegmentType::PlaceholderX;
            } else if (token == 'y') {
                type = SegmentType::PlaceholderY;
            } else {
                literal.push_back(templateUrl[i]);
                ++i;
                continue;
            }

            if (!literal.empty()) {
                segments_.push_back({SegmentType::Literal, literal});
                literal.clear();
            }
            segments_.push_back({type, {}});
            i += 3;
            continue;
        }

        literal.push_back(templateUrl[i]);
        ++i;
    }

    if (!literal.empty()) {
        segments_.push_back({SegmentType::Literal, literal});
    }
}

std::string TileUrlTemplate::Build(int z, int x, int y) const {
    std::string out;
    out.reserve(segments_.size() * 8);
    for (const auto& seg : segments_) {
        switch (seg.type) {
            case SegmentType::Literal:
                out += seg.text;
                break;
            case SegmentType::PlaceholderZ:
                out += std::to_string(z);
                break;
            case SegmentType::PlaceholderX:
                out += std::to_string(x);
                break;
            case SegmentType::PlaceholderY:
                out += std::to_string(y);
                break;
        }
    }
    return out;
}

} // namespace globe
