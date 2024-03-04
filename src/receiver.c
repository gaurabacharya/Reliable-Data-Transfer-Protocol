#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <pthread.h>
#include <errno.h>

#include "./include/packet.h"
#include "./include/packetQueue.h"
#include "./include/writeThreadArgs.h"
#include "./include/packetQueue.c"

void connectSocket(int sockfd, struct sockaddr *senderAddr, socklen_t addrLen, int *seqNum) {
    struct Packet *recvPacket;
    struct Packet *sendPacket;
    int connectionEstablished = 0;

    recvPacket = malloc(sizeof(struct Packet));
    sendPacket = malloc(sizeof(struct Packet));

    // Begin three-way handshake
    while (!connectionEstablished) {
        if (recvfrom(sockfd, recvPacket, sizeof(struct Packet), 0, senderAddr, &addrLen) < 0) {
            perror("Error: Failed to begin three-way handshake.");
            exit(EXIT_FAILURE);
        }

        if (recvPacket->synBit == 1) {
            sendPacket->synBit = 1;
            sendPacket->ackBit = 1;
            sendPacket->seqNum = *seqNum;
            sendPacket->ackNum = recvPacket->seqNum;
            
            if (sendto(sockfd, sendPacket, sizeof(struct Packet), 0, senderAddr, addrLen) < 0) {
                perror("Error: Failed to send second packet in three-way handshake.");
                exit(EXIT_FAILURE);
            }

            if (recvfrom(sockfd, recvPacket, sizeof(struct Packet), 0, senderAddr, &addrLen) < 0) {
                perror("Error: Failed to receive third packet in three-way handshake.");
                exit(EXIT_FAILURE);
            }

            if (recvPacket->ackBit == 1 && recvPacket->ackNum == sendPacket->seqNum) {
                printf("Connection established.\n");
                connectionEstablished = 1;
            }
            else {
                perror("Error: Failed to establish connection at the third stage.");
            }
        }
        else {
            perror("Error: Failed to establish connection at the first stage.");
        }
    }
}

void disconnectSocket(int sockfd, struct sockaddr *senderAddr, socklen_t addrLen, int *seqNum, int *ackNum) {
    struct Packet *recvPacket;
    struct Packet *sendPacket;
    int connectionFinished = 0;

    recvPacket = malloc(sizeof(struct Packet));
    sendPacket = malloc(sizeof(struct Packet));

    // Begin disconnection
    while (!connectionFinished) {
        sendPacket->seqNum = *seqNum;
        sendPacket->ackBit = 1;
        sendPacket->ackNum = *ackNum - 1;
        
        if (sendto(sockfd, sendPacket, sizeof(struct Packet), 0, senderAddr, addrLen) < 0) {
            perror("Error: Failed to send second packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        *seqNum = *seqNum + 1;

        sendPacket->seqNum = *seqNum;
        sendPacket->ackBit = 0;
        sendPacket->ackNum = recvPacket->seqNum;
        sendPacket->finBit = 1;

        if (sendto(sockfd, sendPacket, sizeof(struct Packet), 0, senderAddr, addrLen) < 0) {
            perror("Error: Failed to send third packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        *seqNum = *seqNum + 1;

        if (recvfrom(sockfd, recvPacket, sizeof(struct Packet), 0, senderAddr, &addrLen) < 0) {
            perror("Error: Failed to receive fourth packet during disconnect.");
            exit(EXIT_FAILURE);
        }
        
        if (recvPacket->ackBit == 1 && recvPacket->ackNum == sendPacket->seqNum) {
            printf("Connection terminated.\n");
            connectionFinished = 1;
        }
        else {
            perror("Error: Failed to disconnect at the last stage");
        }
    }
}

void *writeData(void *args) {
    struct WriteThreadArgs *writeArgs = (struct WriteThreadArgs *)args;
    int finished = 0;

    while (!finished) {
        // Check packetQueue for data or packet loss
        if (isPacketQueueEmpty(writeArgs->packetQueue) || writeArgs->packetQueue->packetLoss == 1) {
            continue;
        }

        // Dequeue a packet from the packet queue
        struct Packet packet = dequeuePacket(writeArgs->packetQueue);

        if (packet.finBit == 1) {
            finished = 1;
        }
        else {
            // Check if the write rate is 0 or greater than the packet size, if so write the entire packet to the file
            if (writeArgs->writeRate == 0 || writeArgs->writeRate >= packet.dataSize) {
                fwrite(packet.data, 1, packet.dataSize, writeArgs->file);
                fflush(writeArgs->file);
                sleep(1);
            }
            else {
                // Otherwise, write the packet to the file in chunks
                while (packet.dataSize > 0) {
                    // Check the remaining data size and write rate to determine how many bytes to write
                    size_t bytesToWrite = (writeArgs->writeRate < packet.dataSize) ? writeArgs->writeRate : packet.dataSize;
                    fwrite(packet.data, 1, bytesToWrite, writeArgs->file);
                    fflush(writeArgs->file);
                    packet.dataSize -= bytesToWrite;

                    // Move the remaining data to the beginning of the buffer
                    memmove(packet.data, packet.data + writeArgs->writeRate, packet.dataSize);
                    sleep(1);
                }
            }
        }
    }
}

int receiveData(int sockfd, struct sockaddr *senderAddr, socklen_t addrLen, int *seqNum, char* destinationFile, unsigned long long int writeRate) {
    struct Packet *recvPacket;
    struct Packet *sendPacket;
    struct PacketQueue *packetQueue;
    struct WriteThreadArgs *args;
    int initDisconnect = 0;
    int expectedAckNum = 1;
    pthread_t writeThread;

    recvPacket = malloc(sizeof(struct Packet));
    sendPacket = malloc(sizeof(struct Packet));
    args = malloc(sizeof(struct WriteThreadArgs));
    initPacketQueue(packetQueue, 16);
    
    // Open the file
    FILE* file = fopen(destinationFile, "w+");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to open file %s\n", destinationFile);
        exit(EXIT_FAILURE);
    }

    // Create a thread to write to the file
    args->file = file;
    args->packetQueue = packetQueue;
    args->writeRate = writeRate;
    pthread_create(&writeThread, NULL, &writeData, (void *)&args);

    while (!initDisconnect) {
        if (recvfrom(sockfd, recvPacket, sizeof(struct Packet), 0, senderAddr, &addrLen) < 0) {
            perror("Error: Failed to receive packet during data transfer.");
            exit(EXIT_FAILURE);
        }

        if (recvPacket->finBit == 1) {
            initDisconnect = 1;
            expectedAckNum = recvPacket->seqNum + 1;
        }
        else {
            // Check for congestion
            while (isPacketQueueFull(packetQueue)) {
                sendPacket->seqNum = *seqNum;
                sendPacket->ackBit = 0;
                sendPacket->ackNum = recvPacket->seqNum;
                
                if (sendto(sockfd, sendPacket, sizeof(struct Packet), 0, senderAddr, addrLen) < 0) {
                    perror("Error: Failed to send NACK for invalid space in buffer during data transfer.");
                    exit(EXIT_FAILURE);
                }

                *seqNum++;
            }

            // Check for packet loss
            while (recvPacket->seqNum < expectedAckNum) {
                sendPacket->seqNum = *seqNum;
                sendPacket->ackBit = 0;
                sendPacket->ackNum = expectedAckNum;
                
                if (sendto(sockfd, sendPacket, sizeof(struct Packet), 0, senderAddr, addrLen) < 0) {
                    perror("Error: Failed to send NACK for packet loss during data transfer.");
                    exit(EXIT_FAILURE);
                }

                *seqNum++;

                if (recvfrom(sockfd, recvPacket, sizeof(struct Packet), 0, senderAddr, &addrLen) < 0) {
                    perror("Error: Failed to receive packet during data transfer.");
                    exit(EXIT_FAILURE);
                }
            }
        }

        // Enqueue the packet
        enqueuePacket(packetQueue, *recvPacket);
    }

    pthread_join(writeThread, NULL);
    return recvPacket->seqNum;
}

void rrecv(unsigned short int myUDPport,
           char* destinationFile, 
           unsigned long long int writeRate) {
    int sockfd;
    struct sockaddr_in receiverAddr, senderAddr;
    socklen_t addrLen = sizeof(senderAddr);
    int *seqNum;
    int *ackNum;

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Set up the receiver address
    memset(&receiverAddr, 0, sizeof(receiverAddr));
    receiverAddr.sin_family = AF_INET;
    receiverAddr.sin_port = htons(myUDPport);
    receiverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the socket
    if (bind(sockfd, (struct sockaddr *)&receiverAddr, sizeof(receiverAddr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    // Allocate memory for sequence and acknowledgment numbers
    seqNum = malloc(sizeof(int));
    ackNum = malloc(sizeof(int));
    *seqNum = 1000;

    // Connect to sender
    connectSocket(sockfd, (struct sockaddr *)&senderAddr, addrLen, seqNum);

    // Receive data
    *ackNum = receiveData(sockfd, (struct sockaddr *)&senderAddr, addrLen, seqNum, destinationFile, writeRate);

    // Disconnect from sender
    disconnectSocket(sockfd, (struct sockaddr *)&senderAddr, addrLen, seqNum, ackNum);
}

int main(int argc, char** argv) {
    // This is a skeleton of a main function.
    // You should implement this function more completely
    // so that one can invoke the file transfer from the
    // command line.

    unsigned short int udpPort;

    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (unsigned short int) atoi(argv[1]);
    
    rrecv(udpPort, "testfile", 0);
}
