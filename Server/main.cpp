
#include <stdlib.h> 
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

#include "packetProcessing.cpp"
#include "coreserver.cpp"

#define SERVERTIME 15
int main() {

    printf("UDP Benchmark starting\n");

    std::atomic<bool> serverRunning; 
    std::atomic<bool> processingRunning;
    std::atomic<bool> loggingRunning;
    loggingRunning.store(true);
    processingRunning.store(true);
    serverRunning.store(true); 
    server udpServer; 
    uint16_t timeLeft = SERVERTIME;

    int fd = PacketLogging::init();
    if (fd < 0){
        printf("ERROR opening file\n");
        return -1; 
    }
    printf("File Open\n");

    std::thread packetLoggingThread([&loggingRunning](){
        PacketLogging::packetLogging(loggingRunning);
    });
    printf("Logging thread up\n");

    std::thread packetProcessingThread([&processingRunning, &serverRunning](){
        PacketProcessing::init(processingRunning, serverRunning);
    });
    printf("Processing thread up\n"); 

    std::thread serverThread([&udpServer, &serverRunning](){
        udpServer.runServer(serverRunning);
    }); 
    printf("SERVER will be up for %d seconds\n",timeLeft);

    UdpPacket benchmarkStart;
    benchmarkStart.hostPacketId = 0;
    benchmarkStart.hostReceivedTimestampNS = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    PacketLogging::pushPacket(benchmarkStart);

    while (timeLeft > 0) {
        sleep(1);
        timeLeft--;
    }
    serverRunning.store(false);
    // TODO add benchmark time of when the server finsihed and write it to the logging file 
    serverThread.join();
    printf("SERVER is complete\n");

    UdpPacket benchmarkEnd;
    benchmarkEnd.hostPacketId = 0;
    benchmarkEnd.hostReceivedTimestampNS = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    PacketLogging::pushPacket(benchmarkEnd);

    processingRunning.store(false);
    packetProcessingThread.join();
    printf("Packet Processing thread is complete\n");

    loggingRunning.store(false); 
    packetLoggingThread.join(); 
    PacketLogging::closeLogging(); 
    printf("Packet Logging thread is complete\n");

    printf("UDP Benchmark is Finsihed");
    return 0; 
}
