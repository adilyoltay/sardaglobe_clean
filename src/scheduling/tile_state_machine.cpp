#include "tile_state_machine.h"

namespace globe {

bool TileStateMachine::Advance(Tile& tile, Event event, double currentTime) {
    TileState currentState = tile.state;
    TileState newState = currentState;
    
    switch (event) {
        case Event::Schedule:
            if (currentState == TileState::Unloaded || currentState == TileState::Failed) {
                newState = TileState::Scheduled;
            }
            break;
            
        case Event::FetchStart:
            if (currentState == TileState::Scheduled || currentState == TileState::Fetching) {
                newState = TileState::Fetching;
            }
            break;
            
        case Event::FetchOk:
            if (currentState == TileState::Fetching ||
                currentState == TileState::Scheduled ||
                currentState == TileState::Decoding) {
                newState = TileState::Decoding;
            }
            break;
            
        case Event::FetchFail:
            if (currentState == TileState::Fetching || currentState == TileState::Scheduled) {
                newState = TileState::Failed;
                tile.retryCount++;
                if (currentTime > 0.0) {
                    tile.lastRetryTime = currentTime;
                }
            }
            break;
            
        case Event::DecodeStart:
            // Explicit decode-start signal (allows deterministic lifecycle instrumentation)
            if (currentState == TileState::Fetching ||
                currentState == TileState::Scheduled ||
                currentState == TileState::Decoding) {
                newState = TileState::Decoding;
            }
            break;
            
        case Event::DecodeOk:
            // Allow from Decoding (normal path) or Scheduled (cache-hit path)
            if (currentState == TileState::Decoding || currentState == TileState::Scheduled) {
                newState = TileState::Uploading;
            }
            break;
            
        case Event::DecodeFail:
            // Allow from Decoding (normal path) or Scheduled (cache-hit with corrupt data)
            if (currentState == TileState::Decoding || currentState == TileState::Scheduled) {
                newState = TileState::Failed;
                tile.retryCount++;
                if (currentTime > 0.0) {
                    tile.lastRetryTime = currentTime;
                }
            }
            break;
            
        case Event::UploadStart:
            if (currentState == TileState::Decoding || currentState == TileState::Uploading) {
                newState = TileState::Uploading;
            }
            break;
            
        case Event::UploadOk:
            if (currentState == TileState::Uploading) {
                newState = TileState::Ready;
                // Reset fade for smooth appearance
                tile.fadeStartTime = 0.0;
                tile.fadeAlpha = 0.0f;
                tile.fadeComplete = false;
            }
            break;
            
        case Event::Evict:
            // Can evict from any state
            newState = TileState::Unloaded;
            tile.retryCount = 0;
            break;
            
        case Event::Drop:
            // Queue overflow - mark as failed for retry
            if (currentState != TileState::Ready && currentState != TileState::Unloaded) {
                newState = TileState::Failed;
                tile.retryCount++;
                if (currentTime > 0.0) {
                    tile.lastRetryTime = currentTime;
                }
            }
            break;
    }
    
    if (newState != currentState) {
        tile.state = newState;
        return true;
    }
    return false;
}

const char* TileStateMachine::EventName(Event event) {
    switch (event) {
        case Event::Schedule:    return "Schedule";
        case Event::FetchStart:  return "FetchStart";
        case Event::FetchOk:     return "FetchOk";
        case Event::FetchFail:   return "FetchFail";
        case Event::DecodeStart: return "DecodeStart";
        case Event::DecodeOk:    return "DecodeOk";
        case Event::DecodeFail:  return "DecodeFail";
        case Event::UploadStart: return "UploadStart";
        case Event::UploadOk:    return "UploadOk";
        case Event::Evict:       return "Evict";
        case Event::Drop:        return "Drop";
        default:                 return "Unknown";
    }
}

const char* TileStateMachine::StateName(TileState state) {
    switch (state) {
        case TileState::Unloaded:  return "Unloaded";
        case TileState::Scheduled: return "Scheduled";
        case TileState::Fetching:  return "Fetching";
        case TileState::Decoding:  return "Decoding";
        case TileState::Uploading: return "Uploading";
        case TileState::Ready:     return "Ready";
        case TileState::Failed:    return "Failed";
        default:                   return "Unknown";
    }
}

bool TileStateMachine::IsValidTransition(TileState from, Event event) {
    switch (event) {
        case Event::Schedule:
            return from == TileState::Unloaded || from == TileState::Failed;
        case Event::FetchStart:
            return from == TileState::Scheduled || from == TileState::Fetching;
        case Event::FetchOk:
            return from == TileState::Fetching || from == TileState::Scheduled || from == TileState::Decoding;
        case Event::FetchFail:
            return from == TileState::Fetching || from == TileState::Scheduled;
        case Event::DecodeStart:
            return from == TileState::Fetching || from == TileState::Scheduled || from == TileState::Decoding;
        case Event::DecodeOk:
            return from == TileState::Decoding || from == TileState::Scheduled;
        case Event::DecodeFail:
            return from == TileState::Decoding || from == TileState::Scheduled;
        case Event::UploadStart:
            return from == TileState::Decoding || from == TileState::Uploading;
        case Event::UploadOk:
            return from == TileState::Uploading;
        case Event::Evict:
            return true;  // Can evict from any state
        case Event::Drop:
            return from != TileState::Ready && from != TileState::Unloaded;
        default:
            return false;
    }
}

} // namespace globe
