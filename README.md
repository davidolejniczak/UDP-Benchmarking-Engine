# ESP32 UDP Benchmarking Engine

This project benchmarks UDP packet delivery from an ESP32 over WiFi to a host-side C++ receiver. The ESP32 sends small timestamped packets as fast as the configured send interval allows, and the host server receives, timestamps, parses, queues, and logs the results for later analysis.

The goal is to measure practical tradeoffs between UDP packet rate, payload size, receive timing, packet loss, and jitter using a simple packet format that is easy to inspect and extend.

## Project Layout

```
ESP32/
    CMakeLists.txt
    sdkconfig
    main/
        CMakeLists.txt
        udp_benchmark_main.cpp

Server/
    main.cpp
    coreserver.cpp
    packetProcessing.cpp

```

## Packet Format

Each ESP32 packet starts with a 12-byte little-endian header:

```
uint32_t espPacketId       // 4 bytes
uint64_t espTimestampNS    // 8 bytes
```

The total UDP payload length is configurable with `PAYLOAD_SIZE_BYTES`. Bytes after the first 12 are filler payload used to benchmark packet-size tradeoffs.

The host parses those two ESP fields and adds its own receive-side metadata:

```
hostPacketId               // host-side sequence number
hostReceivedTimestampNS    // host-side receive timestamp
packetSize                 // received UDP payload size in bytes
ESPpacketId                // parsed ESP packet sequence number
ESPsentTimestampNS         // parsed ESP send timestamp
parseValid                 // 1 when the 12-byte ESP header was parsed, otherwise 0
```

Short or malformed packets are still logged. In that case, the host packet ID and host receive timestamp remain valid, while the parsed ESP fields are set to `0`.

## Host Server

The host server listens for UDP packets on port `8010`.

It uses three threads:

```
Core server thread:
    receive UDP packets with recvfrom()
    timestamp captured packets
    push captured packets into the processing queue

Packet processing thread:
    pop received packets
    parse ESP packet ID and ESP timestamp
    push parsed packets into the logging queue

File logging thread:
    write parsed packet metadata to udp_log.csv
```

This keeps parsing and file I/O away from the receive hot path so packet reception is less affected by CSV writing.

### Build and RUN

From the `Server` directory:

```bash
clang++ -std=c++23 main.cpp -o main

./main
```

The server runs for the configured duration in `Server/main.cpp` and writes to Server/udp_log.csv.

## ESP32 Sender

The ESP32 application connects to WiFi as a station and sends UDP packets to the host server.

Target hardware from the current project setup:

```
ESP32-D0WD-V3 revision v3.1
CPU frequency: 160 MHz
Flash: 4 MB
PSRAM: None
Wireless: WiFi + Bluetooth
```

Before building, create `ESP32/.env` from the example file and enter your wifi and IP information to allow the ESP to connect to the server.

```text
WIFI_SSID=YOUR_WIFI_SSID
WIFI_PASSWORD=YOUR_WIFI_PASSWORD
SERVER_IP=YOUR_SERVER_IP
SEND_INTERVAL_US=1000
PAYLOAD_SIZE_BYTES=12
TEST_DURATION_SEC=15
```

`SERVER_IP` should be the IP address of the machine running the host server.

### Build ESP32 Firmware and Flash And Monitor

From the `ESP32` directory, with ESP-IDF loaded:

```bash
idf.py build

idf.py flash monitor
```