#include "tile_mesh_scheduler.h"
#include <algorithm>

namespace globe {

TileMeshScheduler::TileMeshScheduler(const Config& config, DemManager* demManager)
    : config_(config)
    , demManager_(demManager) {
    int workerCount = std::max(1, config_.meshSchedulerWorkers);
    workers_.reserve(workerCount);
    for (int i = 0; i < workerCount; ++i) {
        workers_.emplace_back([this]() { WorkerLoop(); });
    }
}

TileMeshScheduler::~TileMeshScheduler() {
    Shutdown();
}

void TileMeshScheduler::Request(MeshRequest request) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (request.priority == Priority::Urgent) {
            urgentQueue_.push(std::move(request));
        } else {
            normalQueue_.push(std::move(request));
        }
    }
    queueCv_.notify_one();
}

bool TileMeshScheduler::TryGetResult(TileMeshBuilder::BuildResult& out) {
    return results_.TryPop(out);
}

int TileMeshScheduler::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return static_cast<int>(urgentQueue_.size() + normalQueue_.size());
}

void TileMeshScheduler::Shutdown() {
    if (!running_) return;
    running_ = false;
    queueCv_.notify_all();
    results_.Close();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void TileMeshScheduler::WorkerLoop() {
    int urgentProcessed = 0;
    while (running_) {
        MeshRequest request;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this]() {
                return !running_ || !urgentQueue_.empty() || !normalQueue_.empty();
            });

            if (!running_) break;
            if (urgentQueue_.empty() && normalQueue_.empty()) continue;

            if (urgentProcessed >= URGENT_BATCH_SIZE && !normalQueue_.empty()) {
                request = std::move(normalQueue_.top());
                normalQueue_.pop();
                urgentProcessed = 0;
            } else if (!urgentQueue_.empty()) {
                request = std::move(urgentQueue_.top());
                urgentQueue_.pop();
                ++urgentProcessed;
            } else {
                request = std::move(normalQueue_.top());
                normalQueue_.pop();
                urgentProcessed = 0;
            }
        }

        TileMeshBuilder::BuildResult result = TileMeshBuilder::Build(
            request.key,
            request.extent,
            request.edgeMask,
            demManager_,
            config_,
            true
        );
        result.meshRevision = request.meshRevision;
        if (!results_.Push(std::move(result))) {
            return;
        }
    }
}

} // namespace globe
