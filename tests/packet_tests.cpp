#include <cstdint>
#include <iostream>

#include "marketdata/Packet.h"

namespace {

bool check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "packet_tests failed: " << message << "\n";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    auto packet = marketdata::build_packet(3, 42, 64);
    const auto decoded = marketdata::decode_header(packet.data(), packet.size());

    if (!check(decoded.has_value(), "header should decode")) return 1;
    if (!check(decoded->exchange_id == 3, "exchange id mismatch")) return 1;
    if (!check(decoded->sequence_id == 42, "sequence id mismatch")) return 1;
    if (!check(decoded->send_timestamp_ns > 0, "timestamp should be set")) return 1;
    if (!check(packet.size() == 64, "payload size mismatch")) return 1;

    const std::uint8_t too_short[4] = {};
    if (!check(!marketdata::decode_header(too_short, sizeof(too_short)).has_value(), "short header should fail")) return 1;

    std::cout << "packet_tests passed\n";
    return 0;
}
