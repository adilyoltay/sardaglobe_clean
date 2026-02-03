#include "../src/tile_scheduler.h"
#include <iostream>
#include <vector>
#include <unordered_map>

// Mock Fetcher
class MockFetcher : public ITileFetcher {
public:
    void Fetch(const SchedulerKey& key, const TaskParams& params, int priority) override {
        lastFetchKey = key;
        fetchCount++;
    }
    
    void Cancel(const SchedulerKey& key) override {
        lastCancelKey = key;
        cancelCount++;
    }
    
    SchedulerKey lastFetchKey;
    SchedulerKey lastCancelKey;
    int fetchCount = 0;
    int cancelCount = 0;
};

class MockDecoder : public ITileDecoder {
public:
    void Decode(const SchedulerKey& key, std::vector<unsigned char> data) override {
        lastDecodeKey = key;
        decodeCount++;
    }
    SchedulerKey lastDecodeKey;
    int decodeCount = 0;
};

// Simple Assertion Macros
#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                  << ": " << #a << " != " << #b << " (" << (a) << " != " << (b) << ")" << std::endl; \
        std::exit(1); \
    }

#define ASSERT_TRUE(a) \
    if (!(a)) { \
        std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                  << ": " << #a << " is false" << std::endl; \
        std::exit(1); \
    }

// Helper
SchedulerKey SKey(const Tile& t) { return {t.key, "", false}; }

void TestScheduler() {
    std::cout << "Testing Scheduler..." << std::endl;
    MockFetcher fetcher;
    TileScheduler scheduler(&fetcher);
    
    // Create a tile
    Tile tile(0, 0, 0);
    ASSERT_TRUE(tile.loadState == TileLoadState::UNLOADED);
    
    // Schedule
    scheduler.Schedule(&tile, {});
    
    // NEW: Schedule puts it in queue/SCHEDULED state
    ASSERT_EQ(fetcher.fetchCount, 0); 
    ASSERT_TRUE(tile.loadState == TileLoadState::SCHEDULED);
    
    // NEW: Update triggers Fetch
    scheduler.Update([&](const SchedulerKey& k) -> Tile* {
        if (k.tileKey == tile.key) return &tile;
        return nullptr;
    }, 100.0);
    
    ASSERT_EQ(fetcher.fetchCount, 1);
    ASSERT_TRUE(fetcher.lastFetchKey == SKey(tile));
    ASSERT_TRUE(tile.loadState == TileLoadState::FETCHING); // Scheduler sets this immediately
    
    // Simulate Complete
    scheduler.OnFetchComplete(SKey(tile), {1, 2, 3}, true);
    
    // Update
    scheduler.Update([&](const SchedulerKey& k) -> Tile* {
        if (k.tileKey == tile.key) return &tile;
        return nullptr;
    }, 100.0);
    
    ASSERT_TRUE(tile.loadState == TileLoadState::READY); // Update sets this (skipping decode for now)
    ASSERT_EQ(scheduler.GetPendingCount(), 0);
}

void TestCancel() {
    std::cout << "Testing Cancel..." << std::endl;
    MockFetcher fetcher;
    TileScheduler scheduler(&fetcher);
    
    Tile tile(0, 0, 0);
    scheduler.Schedule(&tile, {});
    
    // Trigger Fetch
    scheduler.Update([&](const SchedulerKey& k) -> Tile* { return &tile; }, 100.0);
    
    ASSERT_TRUE(tile.loadState == TileLoadState::FETCHING);
    // ASSERT_EQ(scheduler.GetActiveFetches(), 1);
    
    scheduler.Cancel(&tile);
    
    ASSERT_EQ(fetcher.cancelCount, 1);
    ASSERT_TRUE(fetcher.lastCancelKey == SKey(tile));
    ASSERT_TRUE(tile.loadState == TileLoadState::UNLOADED);
    ASSERT_EQ(scheduler.GetPendingCount(), 0);
}

void TestDecodePath() {
    std::cout << "Testing Decode Path..." << std::endl;
    MockFetcher fetcher;
    MockDecoder decoder;
    TileScheduler scheduler(&fetcher, &decoder);
    
    Tile tile(0, 0, 0);
    scheduler.Schedule(&tile, {});
    scheduler.Update([&](const SchedulerKey& k) -> Tile* { return &tile; }, 100.0); // Trigger Fetch
    
    // Complete Fetch
    scheduler.OnFetchComplete(SKey(tile), {1}, true);
    
    // Trigger Decode
    scheduler.Update([&](const SchedulerKey& k) -> Tile* { return &tile; }, 100.0);
    
    ASSERT_EQ(decoder.decodeCount, 1);
    ASSERT_TRUE(tile.loadState == TileLoadState::DECODING);
    
    // Complete Decode
    scheduler.OnDecodeComplete(SKey(tile), {1, 2, 3, 4}, 1, 1, true);
    
    // Trigger Final Update
    scheduler.Update([&](const SchedulerKey& k) -> Tile* { return &tile; }, 100.0);
    
    ASSERT_TRUE(tile.loadState == TileLoadState::READY);
    ASSERT_EQ(tile.decodedData.size(), 4);
}

void TestRetryBackoff() {
    std::cout << "Testing Retry Backoff..." << std::endl;
    MockFetcher fetcher;
    TileScheduler scheduler(&fetcher);
    
    Tile tile(0, 0, 0);
    tile.retryCount = 0;
    
    scheduler.Schedule(&tile, {});
    scheduler.Update([&](const SchedulerKey& k) -> Tile* { return &tile; }, 100.0);
    
    // Simulate Failure
    scheduler.OnFetchComplete(SKey(tile), {}, false);
    
    // Update
    scheduler.Update([&](const SchedulerKey& k) -> Tile* { return &tile; }, 101.0);
    
    ASSERT_TRUE(tile.loadState == TileLoadState::FAILED);
    ASSERT_EQ(tile.retryCount, 1);
    ASSERT_EQ(tile.lastRetryTime, 101.0);
}

int main() {
    TestScheduler();
    TestCancel();
    TestDecodePath();
    TestRetryBackoff();
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
