#pragma once

#include "../core/config.h"
#include "../core/extent.h"
#include "../core/bounded_queue.h"
#include "../io/download_types.h"
#include "../io/dem_manager.h"
#include "tile_mesh_builder.h"
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>

namespace globe {

// TileMeshScheduler - Async CPU mesh build with priority scheduling
class TileMeshScheduler {
public:
    struct MeshRequest {
        TileKey key;
        Extent extent;
        uint8_t edgeMask = 0;
        Priority priority = Priority::Normal;
        double score = 0.0;
        uint32_t meshRevision = 0;
    };

    static constexpr size_t MAX_RESULT_QUEUE = 64;

    TileMeshScheduler(const Config& config, DemManager* demManager);
    ~TileMeshScheduler();

    void Request(MeshRequest request);
    bool TryGetResult(TileMeshBuilder::BuildResult& out);
    int GetPendingCount() const;
    void Shutdown();

private:
    struct MeshRequestCompare {
        bool operator()(const MeshRequest& a, const MeshRequest& b) const {
            if (a.priority != b.priority) {
                return a.priority < b.priority;
            }
            return a.score < b.score;
        }
    };

    void WorkerLoop();

    const Config& config_;
    DemManager* demManager_ = nullptr;

    std::priority_queue<MeshRequest, std::vector<MeshRequest>, MeshRequestCompare> urgentQueue_;
    std::priority_queue<MeshRequest, std::vector<MeshRequest>, MeshRequestCompare> normalQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{true};
    static constexpr int URGENT_BATCH_SIZE = 4;

    BoundedQueue<TileMeshBuilder::BuildResult> results_{MAX_RESULT_QUEUE};
};

} // namespace globe
