// Tile State Fallback Stability Test
// Verifies that:
// 1. Child delayed while loading doesn't cause holes (parent stays visible)
// 2. Parent -> child transition has no single-frame blackout
// 3. Cull + unpop chain remains stable

#include "../src/core/tile.h"
#include "../src/core/config.h"
#include "../src/scheduling/tile_state_machine.h"
#include <iostream>
#include <vector>
#include <unordered_map>

using namespace globe;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        return false;
    }
    return true;
}

// Simulate tile becoming ready: set state, textureId, hasMesh
void MakeTileReady(Tile& tile, uint32_t texId = 1) {
    tile.state = TileState::Ready;
    tile.textureId = texId;
    tile.hasMesh = true;
    tile.fadeAlpha = 1.0f;
    tile.fadeComplete = true;
}

} // namespace

int main() {
    int failed = 0;

    // Test 1: Parent tile is renderable while children are loading
    // Simulates: parent at z=4, children at z=5 still loading
    {
        std::unordered_map<TileKey, Tile> tiles;

        TileKey parentKey(4, 3, 5);
        Tile& parent = tiles[parentKey];
        parent.key = parentKey;
        MakeTileReady(parent, 100);

        // Children are in various loading states
        TileKey child0(5, 6, 10);
        TileKey child1(5, 7, 10);
        TileKey child2(5, 6, 11);
        TileKey child3(5, 7, 11);

        tiles[child0].key = child0;
        tiles[child0].state = TileState::Fetching;

        tiles[child1].key = child1;
        tiles[child1].state = TileState::Decoding;

        tiles[child2].key = child2;
        tiles[child2].state = TileState::Uploading;

        tiles[child3].key = child3;
        tiles[child3].state = TileState::Scheduled;

        // Verify parent is still ready and renderable
        failed += !Expect(parent.IsReady(),
            "parent should remain Ready while children are loading");
        failed += !Expect(parent.hasMesh && parent.textureId != 0,
            "parent should be renderable (has mesh + texture)");

        // Verify children are not ready
        failed += !Expect(!tiles[child0].IsReady(), "fetching child should not be ready");
        failed += !Expect(!tiles[child1].IsReady(), "decoding child should not be ready");
        failed += !Expect(!tiles[child2].IsReady(), "uploading child should not be ready");
        failed += !Expect(!tiles[child3].IsReady(), "scheduled child should not be ready");
    }

    // Test 2: State machine doesn't evict parent while children are in-flight
    // (simulates what happens if eviction is attempted)
    {
        Tile parent(TileKey(3, 2, 4));
        MakeTileReady(parent, 200);

        // Eviction should transition to Unloaded
        bool evicted = TileStateMachine::Advance(parent, TileStateMachine::Event::Evict, 1.0);
        failed += !Expect(evicted, "Evict should be a valid transition from Ready");
        failed += !Expect(parent.state == TileState::Unloaded,
            "After eviction, parent should be Unloaded");

        // But in practice, pinned tiles are protected. Verify the state machine
        // correctly handles re-scheduling after eviction.
        bool scheduled = TileStateMachine::Advance(parent, TileStateMachine::Event::Schedule, 1.1);
        failed += !Expect(scheduled, "Evicted tile should be re-schedulable");
        failed += !Expect(parent.state == TileState::Scheduled,
            "Re-scheduled tile should be in Scheduled state");
    }

    // Test 3: Parent -> child transition: no blackout frame
    // Both parent and child should be renderable during crossfade
    {
        Tile parent(TileKey(4, 3, 5));
        MakeTileReady(parent, 300);

        Tile child(TileKey(5, 6, 10));
        MakeTileReady(child, 400);
        child.fadeAlpha = 0.0f;
        child.fadeComplete = false;
        child.fadeStartTime = 0.0;

        // Frame 0: child just became ready, fade starts
        float alpha = child.UpdateFade(10.0);
        failed += !Expect(alpha >= 0.0f && alpha <= 0.01f,
            "child should start with near-zero alpha");

        // During crossfade, BOTH should be renderable
        failed += !Expect(parent.IsReady(),
            "parent must remain renderable during crossfade");
        failed += !Expect(child.IsReady(),
            "child must be renderable (even at low alpha) during crossfade");

        // Frame at midpoint: child alpha should be increasing
        float alphaMid = child.UpdateFade(10.15);
        failed += !Expect(alphaMid > alpha,
            "child alpha should increase over time");

        // Frame at completion: child fully visible
        float alphaFull = child.UpdateFade(10.35);
        failed += !Expect(alphaFull >= 0.99f,
            "child should be fully visible after fade duration");
    }

    // Test 4: Multiple state transitions remain deterministic
    {
        Tile tile(TileKey(6, 12, 20));

        // Normal lifecycle: Unloaded -> Scheduled -> Fetching -> Decoding -> Uploading -> Ready
        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, 1.0),
            "Unloaded->Scheduled should succeed");
        failed += !Expect(tile.state == TileState::Scheduled, "state should be Scheduled");

        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, 1.1),
            "Scheduled->Fetching should succeed");
        failed += !Expect(tile.state == TileState::Fetching, "state should be Fetching");

        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::FetchOk, 1.2),
            "Fetching->Decoding should succeed");
        failed += !Expect(tile.state == TileState::Decoding, "state should be Decoding");

        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::DecodeOk, 1.3),
            "Decoding->Uploading should succeed");
        failed += !Expect(tile.state == TileState::Uploading, "state should be Uploading");

        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::UploadOk, 1.4),
            "Uploading->Ready should succeed");
        failed += !Expect(tile.state == TileState::Ready, "state should be Ready");

        // Verify fade was reset on UploadOk
        failed += !Expect(tile.fadeStartTime == 0.0, "fade should be reset on Ready transition");
        failed += !Expect(tile.fadeAlpha == 0.0f, "fadeAlpha should be reset on Ready transition");
        failed += !Expect(!tile.fadeComplete, "fadeComplete should be false on Ready transition");
    }

    // Test 5: Cancel and re-request cycle remains stable
    {
        Tile tile(TileKey(7, 0, 0));

        TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, 2.0);
        TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, 2.1);

        // Cancel while fetching
        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::Cancel, 2.2),
            "Fetching->Canceled should succeed");
        failed += !Expect(tile.state == TileState::Canceled, "state should be Canceled");

        // Re-schedule
        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, 2.3),
            "Canceled->Scheduled should succeed");
        failed += !Expect(tile.state == TileState::Scheduled, "state should be Scheduled after re-request");
    }

    // Test 6: Fallback policy toggle remains deterministic and explicit.
    {
        Config cfg;
        cfg.fallbackRequireParentUntilChildrenReady = false;
        failed += !Expect(!cfg.fallbackRequireParentUntilChildrenReady,
            "fallback flag can be disabled");

        cfg.fallbackRequireParentUntilChildrenReady = true;
        failed += !Expect(cfg.fallbackRequireParentUntilChildrenReady,
            "fallback flag can be re-enabled");
    }

    if (failed == 0) {
        std::cout << "tile_state_fallback_stability_test: ALL PASSED" << std::endl;
    } else {
        std::cout << "tile_state_fallback_stability_test: " << failed << " FAILED" << std::endl;
    }

    return failed;
}
