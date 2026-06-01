#pragma once

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "marketdata/Packet.h"

namespace marketdata {

class RedisClient {
public:
    RedisClient(std::string host, std::uint16_t port)
        : host_(std::move(host)), port_(port) {}

    ~RedisClient() {
        close_socket();
    }

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    void connect_to_server() {
        close_socket();

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* result = nullptr;
        const std::string port_text = std::to_string(port_);
        const int gai_status = getaddrinfo(host_.c_str(), port_text.c_str(), &hints, &result);
        if (gai_status != 0) {
            throw std::runtime_error(std::string("Redis address lookup failed: ") + gai_strerror(gai_status));
        }

        for (addrinfo* entry = result; entry != nullptr; entry = entry->ai_next) {
            fd_ = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
            if (fd_ < 0) {
                continue;
            }

            if (::connect(fd_, entry->ai_addr, entry->ai_addrlen) == 0) {
                freeaddrinfo(result);
                return;
            }

            close_socket();
        }

        freeaddrinfo(result);
        throw std::runtime_error("Redis connect failed");
    }

    void xadd_batch(const std::vector<PacketRecord>& records) {
        if (records.empty()) {
            return;
        }
        if (fd_ < 0) {
            connect_to_server();
        }

        std::string request;
        request.reserve(records.size() * 220);
        for (const PacketRecord& record : records) {
            append_xadd(request, record);
        }

        write_all(request.data(), request.size());
        for (std::size_t i = 0; i < records.size(); ++i) {
            read_response();
        }
    }

    static std::string build_xadd_command(const PacketRecord& record) {
        std::string request;
        append_xadd(request, record);
        return request;
    }

private:
    static void append_bulk(std::string& out, std::string_view value) {
        out += "$";
        out += std::to_string(value.size());
        out += "\r\n";
        out.append(value.data(), value.size());
        out += "\r\n";
    }

    static void append_xadd(std::string& out, const PacketRecord& record) {
        const std::string key = "md:exchange:" + std::to_string(record.exchange_id);
        const std::string exchange_id = std::to_string(record.exchange_id);
        const std::string sequence_id = std::to_string(record.sequence_id);
        const std::string send_timestamp_ns = std::to_string(record.send_timestamp_ns);
        const std::string receive_timestamp_ns = std::to_string(record.receive_timestamp_ns);
        const std::string payload_size = std::to_string(record.payload_size);

        out += "*15\r\n";
        append_bulk(out, "XADD");
        append_bulk(out, key);
        append_bulk(out, "*");
        append_bulk(out, "exchange_id");
        append_bulk(out, exchange_id);
        append_bulk(out, "sequence_id");
        append_bulk(out, sequence_id);
        append_bulk(out, "send_timestamp_ns");
        append_bulk(out, send_timestamp_ns);
        append_bulk(out, "receive_timestamp_ns");
        append_bulk(out, receive_timestamp_ns);
        append_bulk(out, "payload_size");
        append_bulk(out, payload_size);
        append_bulk(out, "source");
        append_bulk(out, "marketdata_server");
    }

    void write_all(const char* data, std::size_t size) {
        std::size_t sent = 0;
        while (sent < size) {
            const ssize_t n = ::send(fd_, data + sent, size - sent, 0);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                close_socket();
                throw std::runtime_error("Redis write failed");
            }
            if (n == 0) {
                close_socket();
                throw std::runtime_error("Redis write made no progress");
            }
            sent += static_cast<std::size_t>(n);
        }
    }

    void read_exact(char* data, std::size_t size) {
        std::size_t read = 0;
        while (read < size) {
            const ssize_t n = ::recv(fd_, data + read, size - read, 0);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                close_socket();
                throw std::runtime_error("Redis read failed");
            }
            if (n == 0) {
                close_socket();
                throw std::runtime_error("Redis connection closed");
            }
            read += static_cast<std::size_t>(n);
        }
    }

    std::string read_line() {
        std::string line;
        char c = '\0';
        while (true) {
            read_exact(&c, 1);
            if (c == '\r') {
                read_exact(&c, 1);
                if (c != '\n') {
                    throw std::runtime_error("Invalid Redis line ending");
                }
                return line;
            }
            line.push_back(c);
        }
    }

    void read_response() {
        char type = '\0';
        read_exact(&type, 1);
        const std::string line = read_line();
        if (type == '-') {
            throw std::runtime_error("Redis error: " + line);
        }
        if (type == '$') {
            const long long bytes = std::stoll(line);
            if (bytes > 0) {
                std::string payload(static_cast<std::size_t>(bytes) + 2, '\0');
                read_exact(payload.data(), payload.size());
            }
        }
    }

    void close_socket() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    std::string host_;
    std::uint16_t port_;
    int fd_ = -1;
};

}  // namespace marketdata
