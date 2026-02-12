#include "../src/scheduling/tile_state_machine.h"
#include <iostream>

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

int main() {
    int failed = 0;

    {
        Tile t(TileKey(3, 2, 1));
        t.state = TileState::Scheduled;
        TileStateMachine::Advance(t, TileStateMachine::Event::Cancel, 1.0);
        failed += !Expect(t.state == TileState::Canceled, "scheduled -> cancel -> canceled");
    }

    {
        Tile t(TileKey(3, 2, 2));
        t.state = TileState::Fetching;
        TileStateMachine::Advance(t, TileStateMachine::Event::Cancel, 1.0);
        failed += !Expect(t.state == TileState::Canceled, "fetching -> cancel -> canceled");
    }

    {
        Tile t(TileKey(3, 2, 3));
        t.state = TileState::Decoding;
        TileStateMachine::Advance(t, TileStateMachine::Event::Cancel, 1.0);
        failed += !Expect(t.state == TileState::Canceled, "decoding -> cancel -> canceled");
    }

    {
        Tile t(TileKey(3, 2, 4));
        t.state = TileState::Uploading;
        TileStateMachine::Advance(t, TileStateMachine::Event::Cancel, 1.0);
        failed += !Expect(t.state == TileState::Canceled, "uploading -> cancel -> canceled");
    }

    {
        Tile t(TileKey(3, 2, 5));
        t.state = TileState::Canceled;
        TileStateMachine::Advance(t, TileStateMachine::Event::Schedule, 1.0);
        failed += !Expect(t.state == TileState::Scheduled, "canceled -> schedule -> scheduled");
    }

    {
        Tile t(TileKey(3, 2, 6));
        t.state = TileState::Ready;
        bool changed = TileStateMachine::Advance(t, TileStateMachine::Event::Cancel, 1.0);
        failed += !Expect(!changed && t.state == TileState::Ready, "ready should ignore cancel");
    }

    failed += !Expect(TileStateMachine::IsValidTransition(TileState::Scheduled, TileStateMachine::Event::Cancel),
                      "valid transition scheduled->cancel");
    failed += !Expect(TileStateMachine::IsValidTransition(TileState::Decoding, TileStateMachine::Event::Cancel),
                      "valid transition decoding->cancel");
    failed += !Expect(!TileStateMachine::IsValidTransition(TileState::Ready, TileStateMachine::Event::Cancel),
                      "invalid transition ready->cancel");
    failed += !Expect(TileStateMachine::IsValidTransition(TileState::Canceled, TileStateMachine::Event::Schedule),
                      "valid transition canceled->schedule");

    if (failed == 0) {
        std::cout << "TileStateMachineCancelTest PASSED\n";
        return 0;
    }

    std::cerr << "TileStateMachineCancelTest FAILED (" << failed << " checks failed)\n";
    return 1;
}
