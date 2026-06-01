#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "marketdata/Packet.h"

namespace {

struct ClientConfig {
    std::uint32_t exchange_id = 0;
    std::string group = "239.10.0.1";
    std::uint16_t port = 9001;
    std::uint32_t duration_sec = 10;
    std::size_t payload_size = 64;
    std::uint64_t rate = 0;
};

std::string require_value(int& index, int argc, char** argv) {
    if (index + 1 >= argc) {
        throw std::invalid_argument(std::string("missing value for ") + argv[index]);
    }
    return argv[++index];
}

ClientConfig parse_args(int argc, char** argv) {
    ClientConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--exchange-id") {
            config.exchange_id = static_cast<std::uint32_t>(std::stoul(require_value(i, argc, argv)));
        } else if (arg == "--group") {
            config.group = require_value(i, argc, argv);
        } else if (arg == "--port") {
            config.port = static_cast<std::uint16_t>(std::stoul(require_value(i, argc, argv)));
        } else if (arg == "--duration-sec") {
            config.duration_sec = static_cast<std::uint32_t>(std::stoul(require_value(i, argc, argv)));
        } else if (arg == "--payload-size") {
            config.payload_size = static_cast<std::size_t>(std::stoull(require_value(i, argc, argv)));
        } else if (arg == "--rate") {
            config.rate = static_cast<std::uint64_t>(std::stoull(require_value(i, argc, argv)));
        } else if (arg == "--help") {
            std::cout << "marketdata_client --exchange-id N --group 239.10.0.N --port PORT --duration-sec 10 --payload-size 64 --rate 0\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown argument: " + arg);
        }
    }

    if (config.payload_size < marketdata::kPacketHeaderBytes) {
        config.payload_size = marketdata::kPacketHeaderBytes;
    }

    return config;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const ClientConfig config = parse_args(argc, argv);

        const int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            throw std::runtime_error("socket creation failed");
        }

        const unsigned char ttl = 1;
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
        const unsigned char loop = 1;
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(config.port);
        if (inet_pton(AF_INET, config.group.c_str(), &addr.sin_addr) != 1) {
            close(fd);
            throw std::runtime_error("invalid multicast group");
        }

        std::vector<std::uint8_t> packet(config.payload_size, 0);
        std::uint64_t sequence_id = 1;
        std::uint64_t sent = 0;

        const auto start = std::chrono::steady_clock::now();
        const auto deadline = start + std::chrono::seconds(config.duration_sec);
        auto next_send = start;
        const auto interval = config.rate == 0
            ? std::chrono::nanoseconds(0)
            : std::chrono::nanoseconds(1'000'000'000LL / static_cast<long long>(config.rate));

        while (std::chrono::steady_clock::now() < deadline) {
            if (interval.count() > 0) {
                const auto now = std::chrono::steady_clock::now();
                if (now < next_send) {
                    std::this_thread::sleep_for(next_send - now);
                }
                next_send += interval;
            }

            marketdata::encode_header(
                marketdata::PacketHeader{config.exchange_id, sequence_id++, marketdata::now_ns()},
                packet.data()
            );

            const ssize_t n = sendto(fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            if (n == static_cast<ssize_t>(packet.size())) {
                ++sent;
            }
        }

        close(fd);
        std::cout << "exchange_id=" << config.exchange_id << " sent_packets=" << sent << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "marketdata_client error: " << ex.what() << "\n";
        return 1;
    }
}
