#pragma once

#include <errno.h> 
#include <stdlib.h> 
#include <iostream>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h> 
#include <unistd.h>

#define MAXBUFF 2048
#define PORT 8010

class server {
    public: 

        // has its own thread
        int runServer(std::atomic<bool>& serverRunning) {
            int sockfd; 
            char buffer[MAXBUFF];
            timeval timeout; 
            timeout.tv_usec = 0;
            timeout.tv_sec = 5;

            if ((sockfd = socket(AF_INET, SOCK_DGRAM,0)) < 0) {
                std::cerr << "ERROR Socket Creation Failure\n" << std::endl; 
                return -1; 
            }

            if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
                std::cerr << "ERROR Receive Timeout Setup Failure\n" << std::endl;
                close(sockfd);
                return -1;
            }

            sockaddr_in server; 
            sockaddr_in client; 
            memset(&server, 0, sizeof(server)); 
            memset(&client, 0, sizeof(client)); 

            server.sin_family = AF_INET; 
            server.sin_addr.s_addr = INADDR_ANY; 
            server.sin_port = htons(PORT); 

            if (bind(sockfd, reinterpret_cast<sockaddr*>(&server), sizeof(server)) < 0) {
                std::cerr << "ERROR Bind Failure\n" << std::endl; 
                close(sockfd);
                return -1;
            }

            socklen_t clientSize = sizeof(client);
            const socklen_t clientAddrlen = sizeof(client); 
            ssize_t bytesReceived; 
            
            //benchmark timing start
            while (serverRunning.load()) {
                clientSize = clientAddrlen; 
                bytesReceived = recvfrom(sockfd, buffer, MAXBUFF, 0,reinterpret_cast<sockaddr*>(&client), &clientSize);
                if (bytesReceived < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }
                    storeError();
                } else {
                    storePacket(buffer, bytesReceived);
                }
            }
            close(sockfd); 
            return 0;
        }
    
        private: 
            void storePacket(const char* buffer, ssize_t bytesReceived) {
                PacketProcessing::pushPacket(UdpPacket::capture(buffer, static_cast<size_t>(bytesReceived)));
            }

            void storeError(){
                PacketProcessing::pushPacket(UdpPacket());
            }
};
