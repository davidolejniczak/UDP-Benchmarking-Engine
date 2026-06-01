#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "marketdata/FeedConfig.h"
#include "marketdata/Packet.h"
#include "marketdata/RedisClient.h"
#include "marketdata/SpscRing.h"

namespace {

struct ServerConfig {
    std::uint32_t duration_sec = 15;
    std::string redis_host = "127.0.0.1";
    std::uint16_t redis_port = 6379;
    std::size_t batch_size = 1024;
    std::size_t ring_size = 1'048'576;
    std::vector<marketdata::FeedConfig> feeds = marketdata::default_feeds();
};

struct Counters {
    std::atomic<std::uint64_t> received{0};
    std::atomic<std::uint64_t> written{0};
    std::atomic<std::uint64_t> dropped{0};
};

std::string require_value(int& index, int argc, char** argv) {
    if (index + 1 >= argc) {
        throw std::invalid_argument(std::string("missing value for ") + argv[index]);
    }
    return argv[++index];
}

ServerConfig parse_args(int argc, char** argv) {
    ServerConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--duration-sec") {
            config.duration_sec = static_cast<std::uint32_t>(std::stoul(require_value(i, argc, argv)));
        } else if (arg == "--redis-host") {
            config.redis_host = require_value(i, argc, argv);
        } else if (arg == "--redis-port") {
            config.redis_port = static_cast<std::uint16_t>(std::stoul(require_value(i, argc, argv)));
        } else if (arg == "--batch-size") {
            config.batch_size = static_cast<std::size_t>(std::stoull(require_value(i, argc, argv)));
        } else if (arg == "--ring-size") {
            config.ring_size = static_cast<std::size_t>(std::stoull(require_value(i, argc, argv)));
        } else if (arg == "--groups") {
            config.feeds = marketdata::parse_groups(require_value(i, argc, argv));
        } else if (arg == "--help") {
            std::cout << "marketdata_server --duration-sec 15 --redis-host 127.0.0.1 --redis-port 6379 --batch-size 1024 --ring-size 1048576 --groups 239.10.0.1:9001,...\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown argument: " + arg);
        }
    }

    if (config.batch_size == 0) {
        throw std::invalid_argument("batch-size must be greater than zero");
    }
    if (config.ring_size < 2) {
        throw std::invalid_argument("ring-size must be at least 2");
    }

    return config;
}

int open_multicast_socket(const marketdata::FeedConfig& feed) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        throw std::runtime_error("socket creation failed");
    }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(feed.port);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        close(fd);
        throw std::runtime_error("multicast bind failed for port " + std::to_string(feed.port));
    }

    ip_mreq membership{};
    if (inet_pton(AF_INET, feed.group.c_str(), &membership.imr_multiaddr) != 1) {
        close(fd);
        throw std::runtime_error("invalid multicast group: " + feed.group);
    }
    membership.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) < 0) {
        close(fd);
        throw std::runtime_error("failed to join multicast group: " + feed.group);
    }

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 100'000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    return fd;
}

void receiver_loop(
    marketdata::FeedConfig feed,
    marketdata::SpscRing<marketdata::PacketRecord>& ring,
    std::atomic<bool>& running,
    Counters& counters
) {
    int fd = -1;
    try {
        fd = open_multicast_socket(feed);
    } catch (const std::exception& ex) {
        std::cerr << "receiver exchange_id=" << feed.exchange_id << " error: " << ex.what() << "\n";
        running.store(false);
        return;
    }

    std::array<std::uint8_t, 2048> buffer{};
    while (running.load(std::memory_order_relaxed)) {
        sockaddr_in client{};
        socklen_t client_len = sizeof(client);
        const ssize_t n = recvfrom(fd, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr*>(&client), &client_len);
        if (n < 0) {
            continue;
        }

        const auto header = marketdata::decode_header(buffer.data(), static_cast<std::size_t>(n));
        if (!header.has_value()) {
            counters.dropped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        marketdata::PacketRecord record;
        record.exchange_id = header->exchange_id;
        record.sequence_id = header->sequence_id;
        record.send_timestamp_ns = header->send_timestamp_ns;
        record.receive_timestamp_ns = marketdata::now_ns();
        record.payload_size = static_cast<std::uint32_t>(n);

        if (ring.push(record)) {
            counters.received.fetch_add(1, std::memory_order_relaxed);
        } else {
            counters.dropped.fetch_add(1, std::memory_order_relaxed);
        }
    }

    close(fd);
}

bool rings_empty(const std::vector<std::unique_ptr<marketdata::SpscRing<marketdata::PacketRecord>>>& rings) {
    for (const auto& ring : rings) {
        if (!ring->empty()) {
            return false;
        }
    }
    return true;
}

void redis_writer_loop(
    const ServerConfig& config,
    std::vector<std::unique_ptr<marketdata::SpscRing<marketdata::PacketRecord>>>& rings,
    std::atomic<bool>& running,
    Counters& counters,
    std::atomic<bool>& writer_ok
) {
    std::vector<marketdata::PacketRecord> batch;
    batch.reserve(config.batch_size);

    try {
        marketdata::RedisClient redis(config.redis_host, config.redis_port);
        redis.connect_to_server();

        while (running.load(std::memory_order_relaxed) || !rings_empty(rings)) {
            batch.clear();
            marketdata::PacketRecord record;

            for (auto& ring : rings) {
                while (batch.size() < config.batch_size && ring->pop(record)) {
                    batch.push_back(record);
                }
                if (batch.size() >= config.batch_size) {
                    break;
                }
            }

            if (batch.empty()) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }

            redis.xadd_batch(batch);
            counters.written.fetch_add(batch.size(), std::memory_order_relaxed);
        }
    } catch (const std::exception& ex) {
        writer_ok.store(false);
        running.store(false);
        std::cerr << "redis writer error: " << ex.what() << "\n";
    }
}

void metrics_loop(std::atomic<bool>& running, Counters& counters) {
    std::uint64_t last_received = counters.received.load();
    std::uint64_t last_written = counters.written.load();

    while (running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const std::uint64_t received = counters.received.load();
        const std::uint64_t written = counters.written.load();
        std::cout << "received_pps=" << (received - last_received)
                  << " redis_write_pps=" << (written - last_written) << "\n";
        last_received = received;
        last_written = written;
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const ServerConfig config = parse_args(argc, argv);

        std::vector<std::unique_ptr<marketdata::SpscRing<marketdata::PacketRecord>>> rings;
        rings.reserve(config.feeds.size());
        for (std::size_t i = 0; i < config.feeds.size(); ++i) {
            rings.push_back(std::make_unique<marketdata::SpscRing<marketdata::PacketRecord>>(config.ring_size));
        }

        Counters counters;
        std::atomic<bool> running{true};
        std::atomic<bool> writer_ok{true};

        const auto start = std::chrono::steady_clock::now();
        std::vector<std::thread> receivers;
        for (std::size_t i = 0; i < config.feeds.size(); ++i) {
            receivers.emplace_back(receiver_loop, config.feeds[i], std::ref(*rings[i]), std::ref(running), std::ref(counters));
        }

        std::thread writer(redis_writer_loop, std::cref(config), std::ref(rings), std::ref(running), std::ref(counters), std::ref(writer_ok));
        std::thread metrics(metrics_loop, std::ref(running), std::ref(counters));

        std::this_thread::sleep_for(std::chrono::seconds(config.duration_sec));
        running.store(false);

        for (auto& receiver : receivers) {
            receiver.join();
        }
        writer.join();
        metrics.join();

        const auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - start).count();
        const std::uint64_t total_received = counters.received.load();
        const std::uint64_t total_written = counters.written.load();

        std::cout << "summary total_received=" << total_received
                  << " total_written_to_redis=" << total_written
                  << " avg_received_pps=" << static_cast<std::uint64_t>(total_received / elapsed)
                  << " avg_redis_write_pps=" << static_cast<std::uint64_t>(total_written / elapsed)
                  << " dropped_packets=" << counters.dropped.load() << "\n";

        return writer_ok.load() ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "marketdata_server error: " << ex.what() << "\n";
        return 1;
    }
}
