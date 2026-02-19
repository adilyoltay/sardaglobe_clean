#pragma once

#include "../../core/config.h"
#include <string>

namespace globe {

struct GeStartupProbeResult {
    bool success = false;
    std::string reason;
    std::string epoch;
    std::string probeNode;
};

GeStartupProbeResult ProbeGeStartupTerrainMode(const Config& config);

} // namespace globe
