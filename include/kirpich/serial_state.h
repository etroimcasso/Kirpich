#pragma once

// The link-cable protocol state. On a serial interrupt the game jumps through a small dispatch
// table indexed by this value: index 0 runs the handshake that elects master/slave, and 1..3 run
// the three transfer phases. The values are the dispatch indices, not a labelled constant in the
// source; the names are port-authored. tests/test_core_enums.cpp drift-checks them against the
// parser-scanned fixture (tests/fixtures/core_enums_expected.h).
//
// The dispatch table has a 5th slot (index 4) pointing at a bare `ret`; the upstream itself tags it
// "XXX Is this used?". The running game never selects it, so it has no enumerator here.

#include <cstdint>

namespace kirpich {

enum class SerialState : uint8_t {
    HANDSHAKE   = 0x00,  // Elect master/slave from the code read off the wire
    RECEIVE     = 0x01,  // Latch the received byte into the RX slot
    EXCHANGE    = 0x02,  // Slave transmits its pending byte, then clocks the next transfer
    ACKNOWLEDGE = 0x03,  // Third transfer phase
};

}  // namespace kirpich
