#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace marketdata {

constexpr std::size_t kPacketHeaderBytes = sizeof(std::uint32_t) + sizeof(std::uint64_t) + sizeof(std::uint64_t);

struct PacketHeader {
    std::uint32_t exchange_id = 0;
    std::uint64_t sequence_id = 0;
    std::uint64_t send_timestamp_ns = 0;
};

struct PacketRecord {
    std::uint32_t exchange_id = 0;
    std::uint64_t sequence_id = 0;
    std::uint64_t send_timestamp_ns = 0;
    std::uint64_t receive_timestamp_ns = 0;
    std::uint32_t payload_size = 0;
};

inline std::uint64_t now_ns() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

inline void write_u32_le(std::uint8_t* dst, std::uint32_t value) {
    dst[0] = static_cast<std::uint8_t>(value & 0xffU);
    dst[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    dst[2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    dst[3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

inline void write_u64_le(std::uint8_t* dst, std::uint64_t value) {
    for (std::size_t i = 0; i < sizeof(std::uint64_t); ++i) {
        dst[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xffU);
    }
}

inline std::uint32_t read_u32_le(const std::uint8_t* src) {
    return static_cast<std::uint32_t>(src[0]) |
           (static_cast<std::uint32_t>(src[1]) << 8U) |
           (static_cast<std::uint32_t>(src[2]) << 16U) |
           (static_cast<std::uint32_t>(src[3]) << 24U);
}

inline std::uint64_t read_u64_le(const std::uint8_t* src) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(std::uint64_t); ++i) {
        value |= static_cast<std::uint64_t>(src[i]) << (i * 8U);
    }
    return value;
}

inline void encode_header(const PacketHeader& header, std::uint8_t* dst) {
    write_u32_le(dst, header.exchange_id);
    write_u64_le(dst + sizeof(std::uint32_t), header.sequence_id);
    write_u64_le(dst + sizeof(std::uint32_t) + sizeof(std::uint64_t), header.send_timestamp_ns);
}

inline std::optional<PacketHeader> decode_header(const std::uint8_t* data, std::size_t size) {
    if (size < kPacketHeaderBytes) {
        return std::nullopt;
    }

    PacketHeader header;
    header.exchange_id = read_u32_le(data);
    header.sequence_id = read_u64_le(data + sizeof(std::uint32_t));
    header.send_timestamp_ns = read_u64_le(data + sizeof(std::uint32_t) + sizeof(std::uint64_t));
    return header;
}

inline std::vector<std::uint8_t> build_packet(std::uint32_t exchange_id, std::uint64_t sequence_id, std::size_t payload_size) {
    if (payload_size < kPacketHeaderBytes) {
        payload_size = kPacketHeaderBytes;
    }

    std::vector<std::uint8_t> packet(payload_size, 0);
    encode_header(PacketHeader{exchange_id, sequence_id, now_ns()}, packet.data());
    return packet;
}

}  // namespace marketdata
