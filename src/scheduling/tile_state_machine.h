#pragma once

#include "../core/tile.h"
#include <atomic>

namespace globe {

// TileStateMachine - Centralized state transitions (GE-style)
// All tile state changes go through this class for consistency and testability
class TileStateMachine {
public:
    // Events that trigger state transitions
    enum class Event {
        Schedule,       // Tile queued for fetch
        FetchStart,     // HTTP request started
        FetchOk,        // Fetch succeeded
        FetchFail,      // Fetch failed
        DecodeStart,    // Decode started
        DecodeOk,       // Decode succeeded
        DecodeFail,     // Decode failed
        UploadStart,    // GPU upload started
        UploadOk,       // Upload succeeded (tile ready)
        Cancel,         // Request canceled while out of view
        Evict,          // Tile evicted from cache
        Drop            // Result dropped due to queue overflow
    };
    
    // Advance tile state based on event
    // Returns true if transition was valid, false if invalid/ignored
    static bool Advance(Tile& tile, Event event, double currentTime = 0.0);
    
    // Get valid next states for a given state (for debugging/testing)
    static const char* EventName(Event event);
    static const char* StateName(TileState state);
    
    // Check if transition is valid
    static bool IsValidTransition(TileState from, Event event);

    // Telemetry: count of accepted transitions from unexpected source states
    // (e.g. FetchOk from Decoding). Non-zero values may indicate pipeline ordering issues.
    static std::atomic<uint64_t> unexpectedTransitions;
};

} // namespace globe
