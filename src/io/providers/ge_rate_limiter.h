// Google Earth Rate Limiter
// Enforces minimum interval between requests to avoid CAPTCHA blocks

#pragma once

#include <chrono>
#include <mutex>

namespace globe {

// Thread-safe minimum-interval rate limiter for GE API requests.
// Google blocks IPs after ~20 rapid requests without proper headers.
// Even with Origin/Referer headers, 200-300ms spacing is recommended.
class GeRateLimiter {
public:
    explicit GeRateLimiter(int minIntervalMs = 250);

    // Block until a request slot is available
    void WaitForSlot();

    // Check if a request can be made now (non-blocking)
    bool CanRequest() const;

    // Get time until next available slot (0 if ready)
    int MillisUntilReady() const;

    // Update interval at runtime
    void SetMinIntervalMs(int ms);

private:
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point lastRequest_;
    std::chrono::milliseconds minInterval_;
};

} // namespace globe
