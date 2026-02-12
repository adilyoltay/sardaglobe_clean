#include "../src/scheduling/tile_scheduler.h"
#include "../src/scheduling/tile_state_machine.h"
#include "../src/debug/network_panel.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

using namespace globe;

namespace {

bool Expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAILED: " << msg << "\n";
        return false;
    }
    return true;
}

} // namespace

namespace globe {

NetworkPanel& NetworkPanel::Instance() {
    static NetworkPanel panel;
    return panel;
}

void NetworkPanel::RecordStart(const TileKey&, RequestType, const std::string&) {}

void NetworkPanel::RecordComplete(const TileKey&, RequestType, bool, long, size_t,
                                  double, bool, const std::string&) {}

} // namespace globe

int main() {
    int failed = 0;

    Config config;
    config.tileUrl = "ngrd://delay=250/{z}/{x}/{y}";
    config.useDiskCache = false;
    config.useMemoryCache = false;
    config.useDecodedMemoryCache = false;
    config.maxConcurrentFetches = 1;
    config.maxConcurrentDecodes = 1;
    config.maxInFlightFetches = 8;

    TileScheduler scheduler(config);
    TileScheduler::TileMap tiles;
    TileKey key(4, 7, 9);
    tiles.emplace(key, Tile(key));
    Tile& tile = tiles.find(key)->second;
    tile.ComputeExtent();

    scheduler.SetUploadCallback([](Tile&) {});

    bool requested = scheduler.Request(key, Priority::Urgent, 1.0f);
    failed += !Expect(requested, "initial request should be accepted");
    TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, 1.0);
    TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, 1.0);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    scheduler.Cancel(key);
    scheduler.Update(tiles, 1.1);
    failed += !Expect(tile.state == TileState::Canceled, "cancel should move tile to canceled");

    // First request eventually returns success (synthetic source is not CURL-cancellable),
    // but scheduler must drop it due canceled state instead of advancing/failing.
    std::this_thread::sleep_for(std::chrono::milliseconds(320));
    scheduler.Update(tiles, 1.4);
    failed += !Expect(tile.state == TileState::Canceled, "late fetch result should not revive canceled tile");
    failed += !Expect(tile.retryCount == 0, "canceled flow should not increment retry counter");

    // Re-enter view: request should clear canceled marker and allow normal lifecycle again.
    bool rerequested = scheduler.Request(key, Priority::Urgent, 1.0f);
    failed += !Expect(rerequested, "re-request after cancel should be accepted");
    TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, 2.0);
    TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, 2.0);
    failed += !Expect(tile.state == TileState::Fetching, "re-request should leave canceled state");

    std::this_thread::sleep_for(std::chrono::milliseconds(320));
    scheduler.Update(tiles, 2.4);
    failed += !Expect(tile.state != TileState::Failed, "re-requested tile should not fail due stale cancel");

    if (failed == 0) {
        std::cout << "TileSchedulerCancelFlowTest PASSED\n";
        return 0;
    }

    std::cerr << "TileSchedulerCancelFlowTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
