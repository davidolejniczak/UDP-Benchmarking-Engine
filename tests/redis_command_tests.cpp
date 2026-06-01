#include <iostream>
#include <string>

#include "marketdata/RedisClient.h"

namespace {

bool check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "redis_command_tests failed: " << message << "\n";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    const marketdata::PacketRecord record{
        .exchange_id = 2,
        .sequence_id = 99,
        .send_timestamp_ns = 1000,
        .receive_timestamp_ns = 2000,
        .payload_size = 64,
    };

    const std::string command = marketdata::RedisClient::build_xadd_command(record);

    if (!check(command.find("XADD") != std::string::npos, "missing XADD")) return 1;
    if (!check(command.find("md:exchange:2") != std::string::npos, "missing stream key")) return 1;
    if (!check(command.find("sequence_id") != std::string::npos, "missing sequence field")) return 1;
    if (!check(command.find("99") != std::string::npos, "missing sequence value")) return 1;
    if (!check(command.find("receive_timestamp_ns") != std::string::npos, "missing receive timestamp field")) return 1;
    if (!check(command.rfind("*15\r\n", 0) == 0, "unexpected RESP array length")) return 1;

    std::cout << "redis_command_tests passed\n";
    return 0;
}
