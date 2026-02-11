#include "io/tile_fetcher.h"
#include "debug/network_panel.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace globe {

// Test stub: tile_fetcher.cpp reports telemetry to NetworkPanel, but this
// regression test only validates fetch/cancel behavior.
NetworkPanel& NetworkPanel::Instance() {
    static NetworkPanel panel;
    return panel;
}

void NetworkPanel::RecordStart(const TileKey&, RequestType, const std::string&) {}

void NetworkPanel::RecordComplete(const TileKey&, RequestType, bool, long, size_t,
                                  double, bool, const std::string&) {}

} // namespace globe

int main() {
    using namespace globe;

    TileFetcher fetcher(1);

    TileKey blockedKey(2, 1, 1);
    FetchRequest blocker;
    blocker.key = blockedKey;
    blocker.url = "ngrd://delay=250/2/1/1";
    fetcher.Fetch(blocker);

    TileKey key(4, 7, 9);
    std::atomic<int> firstCallbacks{0};

    FetchRequest first;
    first.key = key;
    first.url = "ngrd://4/7/9";
    first.onComplete = [&](std::vector<uint8_t>, bool) {
        firstCallbacks.fetch_add(1, std::memory_order_relaxed);
    };
    fetcher.Fetch(first);

    // Cancel before worker reaches the queued request; this should not poison
    // later re-requests for the same key.
    fetcher.Cancel(key);

    std::mutex mutex;
    std::condition_variable cv;
    bool secondDone = false;
    bool secondSuccess = false;

    FetchRequest second;
    second.key = key;
    second.url = "ngrd://4/7/9";
    second.onComplete = [&](std::vector<uint8_t>, bool success) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            secondDone = true;
            secondSuccess = success;
        }
        cv.notify_one();
    };
    fetcher.Fetch(second);

    std::unique_lock<std::mutex> lock(mutex);
    const bool completed = cv.wait_for(lock, std::chrono::seconds(3), [&]() {
        return secondDone;
    });
    lock.unlock();

    fetcher.Shutdown();

    if (!completed) {
        std::cerr << "Timed out waiting for re-request callback" << std::endl;
        return 1;
    }
    if (!secondSuccess) {
        std::cerr << "Re-request callback arrived but failed" << std::endl;
        return 1;
    }
    if (firstCallbacks.load(std::memory_order_relaxed) != 0) {
        std::cerr << "Cancelled stale request unexpectedly completed" << std::endl;
        return 1;
    }

    return 0;
}
