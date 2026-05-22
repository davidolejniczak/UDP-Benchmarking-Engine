#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <unistd.h>

#define STARTNUM 1

inline uint32_t currentHostPacketId = STARTNUM;

inline uint32_t nextHostPacketId() {
    return currentHostPacketId++;
}

class UdpPacket {
public:
    std::vector<uint8_t> rawPayload;
    uint32_t hostPacketId;
    size_t packetSize;
    uint64_t hostReceivedTimestampNS;
    uint32_t ESPpacketId; 
    uint64_t ESPsentTimestampNS; 
    bool parseValid;

    UdpPacket(): 
        hostPacketId(nextHostPacketId()),
        packetSize(0),
        hostReceivedTimestampNS(nowNS()),
        ESPpacketId(0),
        ESPsentTimestampNS(0),
        parseValid(false) {}

    UdpPacket(const char* buffer, size_t bytesReceived): 
        rawPayload(buffer, buffer + bytesReceived),
        hostPacketId(nextHostPacketId()),
        packetSize(bytesReceived),
        hostReceivedTimestampNS(nowNS()),
        ESPpacketId(0), 
        ESPsentTimestampNS(0),
        parseValid(false) {}

    static UdpPacket capture(const char* buffer, size_t bytesReceived) {
        return UdpPacket(buffer, bytesReceived);
    }

private:
    static uint64_t nowNS() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()
        );
    }
};

// has its own thread
class PacketLogging {
public: 
    inline static std::vector<char> loggingBuffer;
    inline static int fd = -1; 

    static void pushPacket(const UdpPacket& packet) {
        std::lock_guard<std::mutex> lock(loggingQueueMutex);
        loggingQueue.push_back(packet);
    }

    static int init(){
        fd = open("udp_log.csv", O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) {
            return -1; 
        }

        loggingBuffer.reserve(1024 * 1024);
        const std::string header = "hostPacketId,hostReceivedTimestampNS,packetSize,ESPpacketId,ESPsentTimestampNS,parseValid\n";
        loggingBuffer.insert(loggingBuffer.end(), header.begin(), header.end());
        return 0; 
    }

    static void closeLogging(){
        if (!loggingBuffer.empty()){ 
            write(fd,loggingBuffer.data(),loggingBuffer.size());
        }
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
    }

    static void packetLogging(std::atomic<bool>& loggingRunning){
        while (loggingRunning.load() || !isQueueEmpty()) {
            std::optional<UdpPacket> packet = popPacket();

            if (!packet.has_value()) {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
                continue;
            }

            appendPacket(*packet);

            if (loggingBuffer.size() >= 1024 * 1024) {
                write(fd, loggingBuffer.data(), loggingBuffer.size());
                loggingBuffer.clear();
            }
        }
    }

private:
    inline static std::deque<UdpPacket> loggingQueue;
    inline static std::mutex loggingQueueMutex;

    static bool isQueueEmpty() {
        std::lock_guard<std::mutex> lock(loggingQueueMutex);
        return loggingQueue.empty();
    }

    static std::optional<UdpPacket> popPacket() {
        std::lock_guard<std::mutex> lock(loggingQueueMutex);

        if (loggingQueue.empty()) {
            return std::nullopt;
        }

        UdpPacket packet = loggingQueue.front();
        loggingQueue.pop_front();
        return packet;
    }

    static void appendPacket(const UdpPacket& packet) {
        const std::string row =
            std::to_string(packet.hostPacketId) + "," +
            std::to_string(packet.hostReceivedTimestampNS) + "," +
            std::to_string(packet.packetSize) + "," +
            std::to_string(packet.ESPpacketId) + "," +
            std::to_string(packet.ESPsentTimestampNS) + "," +
            std::to_string(packet.parseValid ? 1 : 0) + "\n";

        loggingBuffer.insert(loggingBuffer.end(), row.begin(), row.end());
    }
};

// has its own thread 
class PacketProcessing {
public:
    static void pushPacket(const UdpPacket& packet) {
        std::lock_guard<std::mutex> lock(packetProcessingQueueMutex);
        packetProcessingQueue.push_back(packet);
    }

    static void init(std::atomic<bool>& processingRunning, std::atomic<bool>& serverRunning) {
        while (processingRunning.load() || serverRunning.load() || !isQueueEmpty()) {
            std::optional<UdpPacket> packet = popPacket();

            if (!packet.has_value()) {
                std::this_thread::sleep_for(std::chrono::microseconds(1));
                continue;
            }

            PacketLogging::pushPacket(*packet);
        }
    }

    static std::optional<UdpPacket> popPacket() {
        std::lock_guard<std::mutex> lock(packetProcessingQueueMutex);

        if (packetProcessingQueue.empty()) {
            return std::nullopt;
        }

        UdpPacket packet = packetProcessingQueue.front();
        packetProcessingQueue.pop_front();
        parsePacket(packet);

        return packet;
    }

    static bool parsePacket(UdpPacket& packet) {
        if (packet.rawPayload.size() < UDP_PAYLOAD_SIZE) {
            packet.ESPpacketId = 0;
            packet.ESPsentTimestampNS = 0; 
            packet.parseValid = false;
            return true;
        }
        // little endian 
        packet.ESPpacketId =
            static_cast<uint32_t>(packet.rawPayload[0]) |
            (static_cast<uint32_t>(packet.rawPayload[1]) << 8) |
            (static_cast<uint32_t>(packet.rawPayload[2]) << 16) |
            (static_cast<uint32_t>(packet.rawPayload[3]) << 24);

        packet.ESPsentTimestampNS =
            static_cast<uint64_t>(packet.rawPayload[4]) |
            (static_cast<uint64_t>(packet.rawPayload[5]) << 8) |
            (static_cast<uint64_t>(packet.rawPayload[6]) << 16) |
            (static_cast<uint64_t>(packet.rawPayload[7]) << 24) |
            (static_cast<uint64_t>(packet.rawPayload[8]) << 32) |
            (static_cast<uint64_t>(packet.rawPayload[9]) << 40) |
            (static_cast<uint64_t>(packet.rawPayload[10]) << 48) |
            (static_cast<uint64_t>(packet.rawPayload[11]) << 56);

        packet.parseValid = true;
        return true;
    }

private:
    inline static std::deque<UdpPacket> packetProcessingQueue;
    inline static std::mutex packetProcessingQueueMutex;
    static constexpr size_t UDP_PAYLOAD_SIZE = sizeof(uint32_t) + sizeof(uint64_t);

    static bool isQueueEmpty() {
        std::lock_guard<std::mutex> lock(packetProcessingQueueMutex);
        return packetProcessingQueue.empty();
    }
};
