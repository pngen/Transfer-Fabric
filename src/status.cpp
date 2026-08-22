#include "transfer_fabric/status.hpp"

namespace transfer_fabric {

// Exhaustive transition table. Row = from-index, column = to-index.
// State indices are the TransferState enumerator values in declaration order.
const std::array<StateTransitionTable::Row, StateTransitionTable::N>&
StateTransitionTable::table() {
    // N = 11: created, planned, reserved, queued, active, verifying, committing,
    // completed, cancelled, failed, rolled_back.
    static const std::array<Row, N> t = [] {
        std::array<Row, N> m{};
        // created
        m[0][1] = true;  m[0][9] = true;  m[0][8] = true;
        // planned
        m[1][2] = true;  m[1][9] = true;  m[1][8] = true;
        // reserved
        m[2][3] = true;  m[2][10] = true; m[2][8] = true; m[2][9] = true;
        // queued
        m[3][4] = true;  m[3][8] = true;  m[3][9] = true; m[3][10] = true;
        // active
        m[4][5] = true;  m[4][8] = true;  m[4][9] = true; m[4][10] = true;
        // verifying
        m[5][6] = true;  m[5][7] = true;  m[5][8] = true; m[5][9] = true; m[5][10] = true;
        // committing
        m[6][7] = true;  m[6][9] = true;  m[6][10] = true;
        // terminal states (completed, cancelled, failed, rolled_back) have no out edges.
        return m;
    }();
    return t;
}

} // namespace transfer_fabric
