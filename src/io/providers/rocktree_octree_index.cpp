// RockTree Octree Index Implementation

#include "rocktree_octree_index.h"
#include "ge_headers.h"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace globe {

namespace {
constexpr int kMinTmsQuadKeyDepth = 2;

bool IsValidTmsQuadKey(const std::string& key) {
    if (key.size() < static_cast<std::size_t>(kMinTmsQuadKeyDepth)) {
        return false;
    }
    for (char digit : key) {
        if (digit < '0' || digit > '3') {
            return false;
        }
    }
    return true;
}

bool HasPrefix(const std::string& value, const std::string& prefix) {
    return !prefix.empty() &&
           value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

} // namespace

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
    return ge_headers::BuildStandardHeaders();
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

    std::cout << "[Octree] Fetching PlanetoidMetadata from: " << planetoidMetadataUrl_ << std::endl;
    
    auto headers = BuildHeaders();
    std::cout << "[Octree] Request headers:" << std::endl;
    for (const auto& [key, value] : headers) {
        std::cout << "  " << key << ": " << value << std::endl;
    }
    
    HttpResponse response = transport_->Get(planetoidMetadataUrl_, headers);

    std::cout << "[Octree] PlanetoidMetadata response: HTTP " << response.httpCode 
              << ", success=" << response.success 
              << ", body=" << response.body.size() << " bytes" << std::endl;

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
        // Log first 500 chars of body for debugging
        if (!response.body.empty()) {
            std::string preview(response.body.begin(), 
                               response.body.begin() + std::min(size_t(500), response.body.size()));
            std::cerr << "[Octree] Response preview: " << preview << std::endl;
        }
        return false;
    }

    PlanetoidMetadata meta = PlanetoidMetadataParser::Parse(response.body);
    if (!meta.valid) {
        std::cerr << "[Octree] PlanetoidMetadata parse error: " << meta.error << std::endl;
        // Log hex dump of first 100 bytes for debugging
        if (!response.body.empty()) {
            std::cerr << "[Octree] Response hex (first 100 bytes): ";
            for (size_t i = 0; i < std::min(size_t(100), response.body.size()); ++i) {
                printf("%02x ", static_cast<unsigned char>(response.body[i]));
            }
            std::cerr << std::endl;
        }
        return false;
    }

    epoch_ = meta.epoch;
    earthRadiusM_ = static_cast<double>(meta.earthRadiusM);
    std::cout << "[Octree] PlanetoidMetadata OK: epoch=" << epoch_ 
              << " earthRadius=" << earthRadiusM_ << "m" << std::endl;
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

    if (bulk.nodes.empty()) return;

    // Google Earth BulkMetadata uses DENSE QUADTREE (4 children per node).
    // Path digits are 0-3 (quadtree, NOT octree 0-7).
    // Children of node at index i are at indices 4*i+1 through 4*i+4.
    // Flags field 1: bit 0 = hasNodeData, bit 1 = hasBulkMetadata.

    // Build path mapping: index -> quadtree path string
    std::vector<std::string> paths(bulk.nodes.size());
    paths[0] = bulk.basePath;

    for (size_t i = 0; i < bulk.nodes.size(); ++i) {
        for (int child = 0; child < 4; ++child) {
            size_t childIdx = 4 * i + 1 + child;
            if (childIdx < bulk.nodes.size()) {
                paths[childIdx] = paths[i] + std::to_string(child);
            }
        }
    }

    // Store all entries
    for (size_t i = 0; i < bulk.nodes.size(); ++i) {
        const auto& nodeMeta = bulk.nodes[i];
        const std::string& path = paths[i];

        if (path.empty() && i > 0) continue;  // Safety check

        OctreeNodeInfo info;
        info.hasNodeData = nodeMeta.hasNodeData;
        info.hasBulkMetadata = nodeMeta.hasBulkMetadata;
        info.availableChildren = 0x0F;  // Dense quadtree: all 4 children always present
        info.epoch = nodeMeta.epoch;
        
        // Epoch Inheritance: walk ancestor chain until a non-zero epoch is found.
        // Parent may also have epoch 0, so keep climbing.
        if (info.epoch == 0 && !path.empty()) {
            std::string ancestor = path;
            while (info.epoch == 0 && !ancestor.empty()) {
                ancestor = ancestor.substr(0, ancestor.length() - 1);
                auto ait = nodes_.find(ancestor);
                if (ait != nodes_.end() && ait->second.epoch != 0) {
                    info.epoch = ait->second.epoch;
                }
            }
        }
        
        // MSB Alternation Rule:
        // In GE Quadtree, consecutive path digits must have alternating MSBs (bit 1).
        // 0(00), 1(01) -> MSB 0
        // 2(10), 3(11) -> MSB 1
        // If MSB(current) == MSB(prev) for ANY adjacent pair in the path, 
        // the node is invalid (HTTP 400 prevention).
        if (info.hasNodeData && path.length() >= 2) {
            bool validPath = true;
            for (size_t k = 1; k < path.length(); ++k) {
                char curr = path[k];
                char prev = path[k-1];
                int msbCurr = (curr - '0') >> 1;
                int msbPrev = (prev - '0') >> 1;
                
                if (msbCurr == msbPrev) {
                    validPath = false;
                    break;
                }
            }
            
            if (!validPath) {
                info.hasNodeData = false; // Mark as no data to prevent 400
            }
        }

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

uint32_t RockTreeOctreeIndex::GetNodeEpoch(const std::string& path) const {
    std::lock_guard<std::mutex> lock(nodesMutex_);
    // Walk ancestor chain: node → parent → grandparent → ... → global epoch
    std::string p = path;
    while (!p.empty()) {
        auto it = nodes_.find(p);
        if (it != nodes_.end() && it->second.epoch != 0) {
            return it->second.epoch;
        }
        p = p.substr(0, p.length() - 1);
    }
    // Check root node (empty path)
    auto rootIt = nodes_.find("");
    if (rootIt != nodes_.end() && rootIt->second.epoch != 0) {
        return rootIt->second.epoch;
    }
    return epoch_;  // Fallback to global epoch
}

bool RockTreeOctreeIndex::HasBulkMetadataAvailable(const std::string& path) const {
    std::lock_guard<std::mutex> lock(nodesMutex_);
    auto it = nodes_.find(path);
    if (it == nodes_.end()) return false;
    return it->second.hasBulkMetadata;
}

std::vector<std::string> RockTreeOctreeIndex::GetChildrenWithData(
    const std::string& parentPath) const {

    std::lock_guard<std::mutex> lock(nodesMutex_);
    std::vector<std::string> result;

    for (int child = 0; child < 4; child++) {
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
    return CollectRenderableNodesByDepthRange(minDepth, maxDepth);
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
            // Root BulkMetadata is discovered up to a bounded depth window in current implementation.
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
    const int tileDepth = static_cast<int>(tileQuadKey.length());
    if (!IsValidTmsQuadKey(tileQuadKey)) {
        return {};
    }

    // GE RockTree uses a QUADTREE (digits 0-3), same coordinate space as TMS.
    // Prefix matching works: TMS quadkey prefix maps to quadtree face.
    const std::string facePrefix = tileQuadKey.substr(0, kMinTmsQuadKeyDepth);

    if (tileDepth <= kMinTmsQuadKeyDepth) {
        // Short keys: exact depth match only
        return CollectRenderableNodesByDepthRange(tileDepth, tileDepth, facePrefix);
    }

    const int minDepth = std::max(kMinTmsQuadKeyDepth, tileDepth - 1);
    const int maxDepth = tileDepth + 1;
    return CollectRenderableNodesByDepthRange(minDepth, maxDepth, facePrefix);
}

std::vector<std::string> RockTreeOctreeIndex::CollectRenderableNodesByDepthRange(
    int minDepth,
    int maxDepth,
    const std::string& facePrefix) const {

    std::lock_guard<std::mutex> lock(nodesMutex_);
    if (maxDepth < minDepth) {
        return {};
    }

    std::vector<std::vector<std::string>> byDepth;
    byDepth.assign(static_cast<std::size_t>(maxDepth - minDepth + 1), {});
    std::vector<std::string> result;

    const bool filterByPrefix = !facePrefix.empty();
    for (const auto& [path, info] : nodes_) {
        if (!info.hasNodeData) continue;

        const int pathDepth = static_cast<int>(path.length());
        if (pathDepth < minDepth || pathDepth > maxDepth) {
            continue;
        }

        if (filterByPrefix && !HasPrefix(path, facePrefix)) continue;

        byDepth[static_cast<std::size_t>(pathDepth - minDepth)].push_back(path);
    }

    for (auto& bucket : byDepth) {
        std::sort(bucket.begin(), bucket.end());
        for (const auto& path : bucket) {
            result.push_back(path);
        }
    }

    return result;
}


} // namespace globe
