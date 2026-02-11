#pragma once

#include <array>
#include <algorithm>
#include <numeric>
#include <cstddef>

namespace globe {

struct FrameTimings {
    double lodSelectMs = 0.0;
    double requestLoopMs = 0.0;
    double schedulerUpdateMs = 0.0;
    double textureUploadMs = 0.0;
    double demUpdateMs = 0.0;       // DEM pin + update + heightmap upload
    double edgeMaskMs = 0.0;        // Edge mask + DEM coherence computation
    double meshBuildMs = 0.0;
    double renderMs = 0.0;
    double totalMs = 0.0;
    int meshRebuildsQueued = 0;     // Mesh rebuilds queued this frame
};

class FrameTimeTracker {
public:
    void Record(double ms) {
        frameTimes_[writeIndex_] = ms;
        writeIndex_ = (writeIndex_ + 1) % frameTimes_.size();
        if (count_ < frameTimes_.size()) {
            ++count_;
        }
    }

    double GetP95() const {
        return GetPercentile(0.95);
    }

    double GetP99() const {
        return GetPercentile(0.99);
    }

    double GetAvg() const {
        if (count_ == 0) return 0.0;
        double sum = 0.0;
        for (size_t i = 0; i < count_; ++i) {
            sum += frameTimes_[i];
        }
        return sum / static_cast<double>(count_);
    }

private:
    double GetPercentile(double p) const {
        if (count_ == 0) return 0.0;
        std::array<double, 300> copy = frameTimes_;
        size_t n = count_;
        std::sort(copy.begin(), copy.begin() + static_cast<long>(n));
        size_t idx = static_cast<size_t>(std::max(0.0, std::min(p, 1.0)) * (n - 1));
        return copy[idx];
    }

    std::array<double, 300> frameTimes_{};
    size_t writeIndex_ = 0;
    size_t count_ = 0;
};

} // namespace globe
