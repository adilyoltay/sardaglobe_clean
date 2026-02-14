// RockTree Octree Index Implementation

#include "rocktree_octree_index.h"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace globe {

RockTreeOctreeIndex::RockTreeOctreeIndex(const Config& config,
                                         GeRateLimiter* rateLimiter)
    : config_(config),
      rateLimiter_(rateLimiter) {

    // URL templates
    planetoidMetadataUrl_ = config.gePlanetoidMetadataUrl;
    bulkMetadataTemplate_ = config.geBulkMetadataEndpoint;
    nodeDataTemplate_ = config.geMeshEndpoint;

    // Create HTTP/2 transport with shorter timeouts for metadata discovery
    // (fail fast on CAPTCHA blocks instead of hanging 30s)
    HttpTransportConfig httpConfig;
    httpConfig.verifySsl = true;
    httpConfig.timeoutSec = 10;          // 10s total (vs 30s default)
    httpConfig.connectTimeoutSec = 5;    // 5s connect (vs 10s default)
    httpConfig.enableHttp2 = config.geMeshEnableHttp2;
    httpConfig.allowHttp1Fallback = config.geMeshAllowHttp1Fallback;
    httpConfig.tcpKeepAliveSec = config.geMeshTcpKeepAliveSec;
    httpConfig.tcpKeepAliveIdleSec = config.geMeshTcpKeepAliveIdleSec;
    httpConfig.enableConnectionReuse = config.geMeshEnableConnectionReuse;
    transport_ = std::make_unique<CurlHttpTransport>(httpConfig);
}

RockTreeOctreeIndex::~RockTreeOctreeIndex() = default;

std::vector<std::pair<std::string, std::string>>
RockTreeOctreeIndex::BuildHeaders() const {
    return {
        {"Accept", "application/x-protobuf"},
        {"User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                       "AppleWebKit/537.36 (KHTML, like Gecko) "
                       "Chrome/121.0.0.0 Safari/537.36"},
        {"Referer", "https://earth.google.com/"},
        {"Origin", "https://earth.google.com"}
    };
}

bool RockTreeOctreeIndex::Init() {
    // Step 1: Fetch PlanetoidMetadata (epoch + earth radius)
    std::cout << "[Octree] Fetching PlanetoidMetadata from " << planetoidMetadataUrl_ << "..." << std::endl;
    if (!FetchPlanetoidMetadata()) {
        std::cerr << "[Octree] Failed to fetch PlanetoidMetadata" << std::endl;
        return false;
    }

    std::cout << "[Octree] PlanetoidMetadata: epoch=" << epoch_
              << " earthRadius=" << earthRadiusM_ << "m" << std::endl;

    // Step 2: Fetch root BulkMetadata (empty path = root)
    std::cout << "[Octree] Fetching root BulkMetadata..." << std::endl;
    if (!FetchBulkMetadata("")) {
        std::cerr << "[Octree] Failed to fetch root BulkMetadata" << std::endl;
        return false;
    }

    size_t nodeCount = GetKnownNodeCount();
    size_t meshCount = GetMeshNodeCount();
    std::cout << "[Octree] Root BulkMetadata: " << nodeCount << " nodes, "
              << meshCount << " with mesh data" << std::endl;

    initialized_ = true;
    return true;
}

bool RockTreeOctreeIndex::FetchPlanetoidMetadata() {
    if (rateLimiter_) rateLimiter_->WaitForSlot();

    auto headers = BuildHeaders();
    HttpResponse response = transport_->Get(planetoidMetadataUrl_, headers);

    if (!response.success) {
        std::cerr << "[Octree] PlanetoidMetadata HTTP error: "
                  << response.httpCode << " " << response.errorMessage << std::endl;
        return false;
    }

    // Detect CAPTCHA/block: Google returns HTML "Sorry" page on rate-limit
    if (response.httpCode == 403 || response.httpCode == 429 ||
        (!response.body.empty() && response.body.size() > 4 &&
         response.body[0] == '<' && response.body[1] == '!')) {
        std::cerr << "[Octree] PlanetoidMetadata CAPTCHA/block detected (HTTP "
                  << response.httpCode << ", body=" << response.body.size() << " bytes)"
                  << std::endl;
        return false;
    }

    PlanetoidMetadata meta = PlanetoidMetadataParser::Parse(response.body);
    if (!meta.valid) {
        std::cerr << "[Octree] PlanetoidMetadata parse error: " << meta.error << std::endl;
        return false;
    }

    epoch_ = meta.epoch;
    earthRadiusM_ = static_cast<double>(meta.earthRadiusM);
    return true;
}

std::string RockTreeOctreeIndex::BuildBulkMetadataUrl(const std::string& prefix) const {
    // Template: https://kh.google.com/rt/earth/BulkMetadata/pb=!1m2!1s{path}!2u{epoch}
    std::string url = bulkMetadataTemplate_;

    // Replace {path}
    size_t pos = url.find("{path}");
    if (pos != std::string::npos) {
        url.replace(pos, 6, prefix);
    }

    // Replace {epoch}
    pos = url.find("{epoch}");
    if (pos != std::string::npos) {
        url.replace(pos, 7, std::to_string(epoch_));
    }

    return url;
}

std::string RockTreeOctreeIndex::BuildNodeDataUrl(const std::string& path) const {
    // Template: https://kh.google.com/rt/earth/NodeData/pb=!1m2!1s{quadkey}!2u{epoch}!2e1!3u1031!4b0
    std::string url = nodeDataTemplate_;

    // Replace {quadkey} with octree path
    size_t pos = url.find("{quadkey}");
    if (pos != std::string::npos) {
        url.replace(pos, 9, path);
    }

    // Replace {epoch}
    pos = url.find("{epoch}");
    if (pos != std::string::npos) {
        url.replace(pos, 7, std::to_string(epoch_));
    }

    return url;
}

bool RockTreeOctreeIndex::FetchBulkMetadata(const std::string& prefix) {
    if (rateLimiter_) rateLimiter_->WaitForSlot();

    std::string url = BuildBulkMetadataUrl(prefix);
    auto headers = BuildHeaders();

    HttpResponse response = transport_->Get(url, headers);

    if (!response.success) {
        std::cerr << "[Octree] BulkMetadata fetch failed for '" << prefix
                  << "': HTTP " << response.httpCode
                  << " " << response.errorMessage << std::endl;
        return false;
    }

    BulkMetadataResult result = BulkMetadataParser::Parse(response.body, prefix);
    if (!result.valid) {
        std::cerr << "[Octree] BulkMetadata parse error for '" << prefix
                  << "': " << result.error << std::endl;
        return false;
    }

    PopulateNodes(result);

    // Mark this prefix as fetched
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        fetchedBulkPrefixes_.insert(prefix);
        pendingBulkFetches_.erase(prefix);
    }

    return true;
}

void RockTreeOctreeIndex::PopulateNodes(const BulkMetadataResult& bulk) {
    std::lock_guard<std::mutex> lock(nodesMutex_);

    // BulkMetadata entries are indexed in a breadth-first traversal order
    // within the subtree rooted at basePath.
    //
    // Index 0 = the node at basePath itself
    // The children of node at index i are at indices 8*i+1 through 8*i+8
    //
    // We reconstruct the path for each entry by tracking parent relationships.

    // Build path mapping: index -> octree path string
    std::vector<std::string> paths;
    paths.resize(bulk.nodes.size());

    if (!bulk.nodes.empty()) {
        // Index 0 is the root of this subtree
        paths[0] = bulk.basePath;
    }

    // Compute paths for all entries in BFS order
    for (size_t i = 0; i < bulk.nodes.size(); i++) {
        // For each child of node i, compute its path
        for (int child = 0; child < 8; child++) {
            size_t childIdx = 8 * i + 1 + child;
            if (childIdx < bulk.nodes.size()) {
                paths[childIdx] = paths[i] + std::to_string(child);
            }
        }
    }

    // Store all entries
    for (size_t i = 0; i < bulk.nodes.size(); i++) {
        const auto& nodeMeta = bulk.nodes[i];
        const std::string& path = paths[i];

        if (path.empty() && i > 0) continue;  // Safety check

        OctreeNodeInfo info;
        info.hasNodeData = nodeMeta.hasNodeData;
        info.hasBulkMetadata = nodeMeta.hasBulkMetadata;
        info.availableChildren = nodeMeta.availableChildren;
        info.epoch = nodeMeta.epoch;
        info.bulkMetadataFetched = false;

        nodes_[path] = info;
    }

    // Mark the basePath subtree as having BulkMetadata fetched
    auto it = nodes_.find(bulk.basePath);
    if (it != nodes_.end()) {
        it->second.bulkMetadataFetched = true;
    }
}

bool RockTreeOctreeIndex::HasNodeData(const std::string& path) const {
    std::lock_guard<std::mutex> lock(nodesMutex_);
    auto it = nodes_.find(path);
    if (it == nodes_.end()) return false;
    return it->second.hasNodeData;
}

uint8_t RockTreeOctreeIndex::GetAvailableChildren(const std::string& path) const {
    std::lock_guard<std::mutex> lock(nodesMutex_);
    auto it = nodes_.find(path);
    if (it == nodes_.end()) return 0;
    return it->second.availableChildren;
}

std::vector<std::string> RockTreeOctreeIndex::GetChildrenWithData(
    const std::string& parentPath) const {

    std::lock_guard<std::mutex> lock(nodesMutex_);
    std::vector<std::string> result;

    for (int child = 0; child < 8; child++) {
        std::string childPath = parentPath + std::to_string(child);
        auto it = nodes_.find(childPath);
        if (it != nodes_.end() && it->second.hasNodeData) {
            result.push_back(childPath);
        }
    }

    return result;
}

std::vector<std::string> RockTreeOctreeIndex::GetRenderableNodes(
    int minDepth, int maxDepth) const {

    std::lock_guard<std::mutex> lock(nodesMutex_);
    if (maxDepth < minDepth) {
        return {};
    }

    std::vector<std::vector<std::string>> byDepth;
    byDepth.assign(static_cast<std::size_t>(maxDepth - minDepth + 1), {});
    std::vector<std::string> result;
    for (const auto& [path, info] : nodes_) {
        int depth = static_cast<int>(path.length());
        if (depth >= minDepth && depth <= maxDepth && info.hasNodeData) {
            byDepth[static_cast<std::size_t>(depth - minDepth)].push_back(path);
        }
    }
    for (auto& bucket : byDepth) {
        std::sort(bucket.begin(), bucket.end());
        for (const auto& path : bucket) {
            result.push_back(path);
        }
    }

    return result;
}

bool RockTreeOctreeIndex::RequestBulkMetadata(const std::string& prefix) {
    std::lock_guard<std::mutex> lock(pendingMutex_);

    // Already fetched?
    if (fetchedBulkPrefixes_.count(prefix) > 0) return false;

    // Already pending?
    if (pendingBulkFetches_.count(prefix) > 0) return false;

    // Check max pending limit
    if (static_cast<int>(pendingBulkFetches_.size()) >= config_.geBulkMetadataMaxPending) {
        return false;
    }

    pendingBulkFetches_.insert(prefix);
    return true;
}

int RockTreeOctreeIndex::ProcessPendingFetches() {
    // Get a copy of pending set
    std::vector<std::string> toFetch;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        for (const auto& prefix : pendingBulkFetches_) {
            toFetch.push_back(prefix);
        }
    }

    if (toFetch.empty()) return 0;

    int completed = 0;
    for (const auto& prefix : toFetch) {
        if (FetchBulkMetadata(prefix)) {
            completed++;
        }
    }

    return completed;
}

bool RockTreeOctreeIndex::IsBulkMetadataFetched(const std::string& path) const {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    // Check if any ancestor prefix covers this path
    for (const auto& prefix : fetchedBulkPrefixes_) {
        if (path.find(prefix) == 0 || prefix.empty()) {
            // Check if this path is within the BFS depth covered by the prefix
            // Root BulkMetadata covers ~4 levels deep
            int depthFromPrefix = static_cast<int>(path.length() - prefix.length());
            if (depthFromPrefix <= 4) return true;
        }
    }
    return false;
}

size_t RockTreeOctreeIndex::GetKnownNodeCount() const {
    std::lock_guard<std::mutex> lock(nodesMutex_);
    return nodes_.size();
}

size_t RockTreeOctreeIndex::GetMeshNodeCount() const {
    std::lock_guard<std::mutex> lock(nodesMutex_);
    size_t count = 0;
    for (const auto& [path, info] : nodes_) {
        if (info.hasNodeData) count++;
    }
    return count;
}

std::vector<std::string> RockTreeOctreeIndex::TileQuadKeyToOctreePaths(
    const std::string& tileQuadKey) const {

    std::lock_guard<std::mutex> lock(nodesMutex_);
    auto isValidTmsQuadKey = [](const std::string& key) {
        if (key.size() < 2) return false;
        for (char digit : key) {
            if (digit < '0' || digit > '3') {
                return false;
            }
        }
        return true;
    };

    std::vector<std::string> result;
    std::vector<std::vector<std::string>> byDepth;

    // TMS quadkeys use digits 0-3, octree uses 0-7
    // At level 2, TMS quadkeys happen to map to valid octree paths
    // (02, 03, 12, 13, 20, 21, 30, 31 are the 8 root faces)
    //
    // For deeper levels, map tile keys to candidate octree nodes by:
    // 1) shared face prefix (first 2 chars),
    // 2) candidate depth window [tileDepth-1, tileDepth+1],
    // 3) deterministic sorting by depth then lexicographic order.

    const int tileDepth = static_cast<int>(tileQuadKey.length());
    if (!isValidTmsQuadKey(tileQuadKey)) {
        return result;
    }

    const int minDepth = std::max(2, tileDepth - 1);
    const int maxDepth = tileDepth + 1;
    byDepth.assign(static_cast<std::size_t>(maxDepth - minDepth + 1), {});

    // For short keys, direct lookup
    if (tileDepth <= 2) {
        auto it = nodes_.find(tileQuadKey);
        if (it != nodes_.end() && it->second.hasNodeData) {
            result.push_back(tileQuadKey);
        }
        return result;
    }

    // For deeper tiles, find octree nodes with the same face prefix and
    // stable, deterministic output ordering.
    // Result is sorted deterministically by depth, then lexicographically.
    std::string facePrefix = tileQuadKey.substr(0, 2);

    for (const auto& [path, info] : nodes_) {
        if (!info.hasNodeData) continue;

        int pathDepth = static_cast<int>(path.length());
        if (pathDepth < minDepth || pathDepth > maxDepth ||
            path.size() < 2 ||
            path.compare(0, 2, facePrefix) != 0) {
            continue;
        }

        byDepth[static_cast<std::size_t>(pathDepth - minDepth)].push_back(path);
    }

    for (auto& bucket : byDepth) {
        std::sort(bucket.begin(), bucket.end());
        for (const auto& candidate : bucket) {
            result.push_back(candidate);
        }
    }

    return result;
}

// Static helper: build octree path from BulkMetadata index
std::string RockTreeOctreeIndex::BuildOctreePath(const std::string& basePath,
                                                  int nodeIndex, int level) {
    if (nodeIndex == 0) return basePath;

    // Reverse-compute path from BFS index
    std::string suffix;
    int idx = nodeIndex;
    while (idx > 0) {
        int parent = (idx - 1) / 8;
        int childDigit = (idx - 1) % 8;
        suffix = std::to_string(childDigit) + suffix;
        idx = parent;
    }

    return basePath + suffix;
}

} // namespace globe
