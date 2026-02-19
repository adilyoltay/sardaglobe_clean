#include "ge_startup_probe.h"

#include "google_earth_nodedata_client.h"
#include "rocktree_node_data_parser.h"
#include "rocktree_octree_index.h"

#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <vector>

namespace globe {

GeStartupProbeResult ProbeGeStartupTerrainMode(const Config& config) {
    GeStartupProbeResult result;

    if (!config.geMeshEnabled()) {
        result.success = false;
        result.reason = "endpoint_missing";
        return result;
    }

    auto hasDeadline = [](const auto& deadline) {
        return std::chrono::steady_clock::now() < deadline;
    };

    const int timeoutSec = std::max(1, config.geStartupProbeTimeoutSec);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);

    RockTreeOctreeIndex index(config);
    if (!index.Init()) {
        result.success = false;
        result.reason = "planetoid_or_bulk_fail";
        return result;
    }

    const std::string epoch = index.GetEpochString();
    if (epoch.empty()) {
        result.success = false;
        result.reason = "planetoid_or_bulk_fail";
        return result;
    }
    result.epoch = epoch;

    std::vector<std::string> candidates;
    std::unordered_set<std::string> seen;
    const auto addCandidate = [&](const std::string& key) {
        if (key.empty()) return;
        if (seen.insert(key).second) {
            candidates.push_back(key);
        }
    };

    // 1) explicit startup probe key
    addCandidate(config.geStartupProbeNodeKey);

    // 2) configured seed quadkeys (first few, stable order)
    const size_t maxSeedCandidates = config.geMeshQuadKeys.empty() ? 0u
                                                                 : std::min(config.geMeshQuadKeys.size(), size_t(8));
    for (size_t i = 0; i < maxSeedCandidates; ++i) {
        addCandidate(config.geMeshQuadKeys[i]);
    }

    // 3) fallback candidate set from octree renderable nodes
    const auto renderableNodes = index.GetRenderableNodes(2, 6);
    for (const auto& node : renderableNodes) {
        addCandidate(node);
    }

    if (candidates.empty()) {
        result.success = false;
        result.reason = "no_valid_candidate";
        return result;
    }

    GoogleEarthNodeDataClient client(config);
    client.SetEpoch(epoch);

    bool anyNodeDataFetched = false;
    bool anyInvalidPayload = false;

    for (const auto& nodeKey : candidates) {
        if (!hasDeadline(deadline)) {
            result.success = false;
            result.reason = "timeout";
            return result;
        }

        const auto nodeResult = client.FetchNodeData(nodeKey);
        if (!nodeResult.success || nodeResult.data.empty()) {
            anyNodeDataFetched = anyNodeDataFetched || nodeResult.success;
            continue;
        }

        anyNodeDataFetched = true;
        const auto parsed = RockTreeNodeDataParser::Parse(nodeResult.data);

        if (!parsed.success || parsed.vertexCount <= 0 || parsed.indices.empty()) {
            anyInvalidPayload = true;
            continue;
        }

        result.success = true;
        result.reason = "ok";
        result.probeNode = nodeKey;
        return result;
    }

    result.success = false;
    if (!anyNodeDataFetched) {
        result.reason = "node_fetch_failed";
    } else if (anyInvalidPayload) {
        result.reason = "parse_failed";
    } else {
        result.reason = "no_valid_candidate";
    }

    return result;
}

} // namespace globe
