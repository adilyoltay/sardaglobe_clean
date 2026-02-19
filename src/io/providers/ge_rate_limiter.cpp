// GeRateLimiter Implementation

#include "ge_rate_limiter.h"
#include <thread>

namespace globe {

GeRateLimiter::GeRateLimiter(int minIntervalMs)
    : lastRequest_(),  // Default-constructed = epoch (zero), first WaitForSlot passes instantly
      minInterval_(minIntervalMs) {
}

void GeRateLimiter::WaitForSlot() {
    std::unique_lock<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRequest_);

    if (elapsed < minInterval_) {
        auto waitTime = minInterval_ - elapsed;
        lock.unlock();
        std::this_thread::sleep_for(waitTime);
        lock.lock();
    }

    lastRequest_ = std::chrono::steady_clock::now();
}

bool GeRateLimiter::CanRequest() const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRequest_);

    return elapsed >= minInterval_;
}

int GeRateLimiter::MillisUntilReady() const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRequest_);

    if (elapsed >= minInterval_) return 0;
    return static_cast<int>((minInterval_ - elapsed).count());
}

void GeRateLimiter::SetMinIntervalMs(int ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    minInterval_ = std::chrono::milliseconds(ms);
}

} // namespace globe
