// Pipeline Integration Smoke Test (GE Parity Stabilization Aşama 4.2)
// Verifies that tile lifecycle transitions remain consistent:
// - placeholder → fallback → crossfade sequence doesn't break
// - Tile state machine transitions for normal load/unload cycle
// - surfaceVertexCount is correctly tracked
// - Config defaults for stabilization features

#include "../src/core/tile.h"
#include "../src/core/config.h"
#include "../src/scheduling/tile_state_machine.h"
#include <iostream>
#include <vector>

using namespace globe;

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main() {
    int failed = 0;

    // Test 1: Full tile lifecycle (Unloaded -> Ready -> Evict -> Re-schedule)
    {
        Tile tile(TileKey(5, 10, 15));
        double t = 100.0;

        // Unloaded -> Scheduled
        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, t),
            "lifecycle: Unloaded->Scheduled");
        failed += !Expect(tile.state == TileState::Scheduled, "lifecycle: state=Scheduled");

        // Scheduled -> Fetching
        t += 0.01;
        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, t),
            "lifecycle: Scheduled->Fetching");

        // Fetching -> Decoding
        t += 0.1;
        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::FetchOk, t),
            "lifecycle: Fetching->Decoding");

        // Decoding -> Uploading
        t += 0.05;
        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::DecodeOk, t),
            "lifecycle: Decoding->Uploading");

        // Uploading -> Ready
        t += 0.01;
        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::UploadOk, t),
            "lifecycle: Uploading->Ready");
        failed += !Expect(tile.state == TileState::Ready, "lifecycle: state=Ready");

        // Simulate rendering with texture
        tile.textureId = 42;
        tile.hasMesh = true;
        failed += !Expect(tile.IsReady(), "lifecycle: IsReady() after setup");

        // Evict
        t += 10.0;
        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::Evict, t),
            "lifecycle: Ready->Evict");
        failed += !Expect(tile.state == TileState::Unloaded, "lifecycle: state=Unloaded");
        failed += !Expect(tile.retryCount == 0, "lifecycle: retryCount reset on evict");

        // Re-schedule
        t += 0.01;
        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, t),
            "lifecycle: Unloaded->Scheduled (re-request)");
    }

    // Test 2: Fail and retry lifecycle
    {
        Tile tile(TileKey(3, 2, 4));
        double t = 200.0;

        TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, t);
        TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, t + 0.01);

        // Fetch fails
        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::FetchFail, t + 0.5),
            "retry: FetchFail should succeed");
        failed += !Expect(tile.state == TileState::Failed, "retry: state=Failed");
        failed += !Expect(tile.retryCount == 1, "retry: retryCount=1");

        // Re-schedule from Failed
        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, t + 30.0),
            "retry: Failed->Scheduled");
    }

    // Test 3: Drop event (queue overflow)
    {
        Tile tile(TileKey(4, 5, 6));
        double t = 300.0;

        TileStateMachine::Advance(tile, TileStateMachine::Event::Schedule, t);
        TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, t + 0.01);

        failed += !Expect(TileStateMachine::Advance(tile, TileStateMachine::Event::Drop, t + 0.02),
            "drop: should succeed from Fetching");
        failed += !Expect(tile.state == TileState::Failed, "drop: state=Failed");
        failed += !Expect(tile.retryCount == 1, "drop: retryCount=1");
    }

    // Test 4: Crossfade simulation - parent and child overlap
    {
        Tile parent(TileKey(4, 3, 5));
        parent.state = TileState::Ready;
        parent.textureId = 100;
        parent.hasMesh = true;
        parent.fadeAlpha = 1.0f;
        parent.fadeComplete = true;

        Tile child(TileKey(5, 6, 10));
        child.state = TileState::Ready;
        child.textureId = 200;
        child.hasMesh = true;
        child.fadeAlpha = 0.0f;
        child.fadeComplete = false;

        // Simulate 10 frames of crossfade
        double startTime = 50.0;
        float prevAlpha = 0.0f;
        for (int frame = 0; frame < 10; ++frame) {
            double t = startTime + frame * 0.033; // ~30fps
            float alpha = child.UpdateFade(t, 0.3f);
            failed += !Expect(alpha >= prevAlpha,
                "crossfade: alpha must be monotonically increasing");
            prevAlpha = alpha;
        }
        failed += !Expect(child.fadeComplete || prevAlpha > 0.8f,
            "crossfade: should be nearly complete after 10 frames");
    }

    // Test 5: Config stabilization defaults
    {
        Config cfg;

        // Aşama 0 defaults
        failed += !Expect(cfg.forceClampTerrainNoData == true,
            "config: forceClampTerrainNoData default=true");
        failed += !Expect(cfg.demNoDataMinHeightM == -11000.0f,
            "config: demNoDataMinHeightM default=-11000");
        failed += !Expect(cfg.demNoDataReplacementM == 0.0f,
            "config: demNoDataReplacementM default=0");
        failed += !Expect(cfg.fallbackRequireParentUntilChildrenReady == true,
            "config: fallbackRequireParentUntilChildrenReady default=true");
        cfg.fallbackRequireParentUntilChildrenReady = false;
        failed += !Expect(cfg.fallbackRequireParentUntilChildrenReady == false,
            "config: fallbackRequireParentUntilChildrenReady can be disabled");

        // Existing critical defaults
        failed += !Expect(cfg.useRteRender == true,
            "config: useRteRender default=true");
        failed += !Expect(cfg.selectiveSkirts == true,
            "config: selectiveSkirts default=true");
        failed += !Expect(cfg.lodChildQuorum == true,
            "config: lodChildQuorum default=true");
    }

    // Test 6: surfaceVertexCount in Tile struct
    {
        Tile tile(TileKey(6, 0, 0));
        failed += !Expect(tile.surfaceVertexCount == 0,
            "surfaceVertexCount default should be 0");

        tile.surfaceVertexCount = 289; // 17x17 grid
        failed += !Expect(tile.surfaceVertexCount == 289,
            "surfaceVertexCount should be settable");
    }

    // Test 7: Invalid state transitions are rejected
    {
        Tile tile(TileKey(7, 1, 1));
        // Tile starts as Unloaded

        // Can't FetchStart from Unloaded
        failed += !Expect(!TileStateMachine::Advance(tile, TileStateMachine::Event::FetchStart, 1.0),
            "invalid: FetchStart from Unloaded should be rejected");
        failed += !Expect(tile.state == TileState::Unloaded,
            "invalid: state should remain Unloaded");

        // Can't UploadOk from Unloaded
        failed += !Expect(!TileStateMachine::Advance(tile, TileStateMachine::Event::UploadOk, 1.0),
            "invalid: UploadOk from Unloaded should be rejected");
    }

    if (failed == 0) {
        std::cout << "pipeline_integration_smoke_test: ALL PASSED" << std::endl;
    } else {
        std::cout << "pipeline_integration_smoke_test: " << failed << " FAILED" << std::endl;
    }

    return failed;
}
