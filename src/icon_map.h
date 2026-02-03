#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace earth {

struct IconInfo {
    std::string name;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    float pixelRatio = 1.0f;
};

struct IconMap {
    std::string name;
    std::string imageUrl;
    std::string jsonUrl;
    uint32_t textureId = 0;
    int width = 0;
    int height = 0;
    std::unordered_map<std::string, IconInfo> icons;
    bool loaded = false;
};

} // namespace earth
