#include "tf_test.hpp"
#include "transfer_fabric/runtime.hpp"
#include "transfer_fabric/transfer_id.hpp"

#include <cstring>
#include <vector>

using namespace transfer_fabric;

TF_TEST(smoke_host_to_host) {
    Runtime rt({});
    const std::size_t sz = 64 * 1024;
    std::vector<std::uint8_t> src(sz), dst(sz, 0);
    for (std::size_t i = 0; i < sz; ++i) src[i] = static_cast<std::uint8_t>((i * 7 + 13) & 0xFF);

    EndpointHandle se, de;
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(src.data(), src.size()), se).ok());
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(dst.data(), dst.size()), de).ok());

    TransferOptions opts;
    opts.source = se;
    opts.destination = de;
    opts.policy.integrity_mode = VerificationMode::crc32c;
    Error err;
    TransferHandle h = rt.submit(opts, err);
    TF_REQUIRE(err.ok());
    TF_REQUIRE(h.valid());
    bool done = rt.wait(h);
    TF_CHECK(done);
    TransferStatus st = rt.status(h);
    TF_EQ(static_cast<int>(st.state), static_cast<int>(TransferState::completed));
    TF_CHECK(st.bytes_completed == sz);
    TF_CHECK(std::memcmp(src.data(), dst.data(), sz) == 0);
    rt.shutdown();
}

TF_TEST(smoke_zero_byte) {
    Runtime rt({});
    std::vector<std::uint8_t> src(1), dst(1);
    EndpointHandle se, de;
    rt.register_endpoint(EndpointDescriptor::host_memory(src.data(), 0), se);
    rt.register_endpoint(EndpointDescriptor::host_memory(dst.data(), 0), de);
    TransferOptions opts; opts.source = se; opts.destination = de;
    Error err;
    TransferHandle h = rt.submit(opts, err);
    TF_REQUIRE(err.ok());
    bool done = rt.wait(h);
    TF_CHECK(done);
    TF_EQ(static_cast<int>(rt.status(h).state), static_cast<int>(TransferState::completed));
    rt.shutdown();
}

TF_TEST(smoke_state_transition_table_exhaustive) {
    // Every terminal state must have zero outgoing transitions; the happy path
    // must be fully connected.
    for (int f = 0; f < 11; ++f) {
        for (int t = 0; t < 11; ++t) {
            TransferState from = static_cast<TransferState>(f);
            TransferState to = static_cast<TransferState>(t);
            if (is_terminal(from)) {
                // only self/terminal stay are disallowed; no terminal has an outgoing edge
                if (StateTransitionTable::allowed(from, to)) {
                    TF_CHECK(false);
                }
            }
        }
    }
    TF_CHECK(StateTransitionTable::allowed(TransferState::created, TransferState::planned));
    TF_CHECK(StateTransitionTable::allowed(TransferState::planned, TransferState::reserved));
    TF_CHECK(StateTransitionTable::allowed(TransferState::reserved, TransferState::queued));
    TF_CHECK(StateTransitionTable::allowed(TransferState::queued, TransferState::active));
    TF_CHECK(StateTransitionTable::allowed(TransferState::active, TransferState::verifying));
    TF_CHECK(StateTransitionTable::allowed(TransferState::verifying, TransferState::completed));
}

TF_TEST(smoke_transfer_id_parse) {
    TransferId id(0x1234567890ABCDEFULL, 0xFEDCBA0987654321ULL);
    std::string s = id.to_string();
    TF_EQ(s.size(), 32u);
    TransferId parsed;
    TF_REQUIRE(TransferId::parse(s, parsed));
    TF_CHECK(parsed == id);
    TransferId bad;
    TF_CHECK(!TransferId::parse("nothex", bad));
    TF_CHECK(TransferId::generate().valid());
}

TF_TEST_MAIN()
