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

/**
 * @brief connectSocket is a function that initializes a three-way handshake connection with a sender.
 * 
 * @param sockfd        The file descriptor of the socket
 * @param senderAddr    The address of the sender
 * @param addrLen       The length of the sender address
 * @param seqNum        The sequence number of the receiver
 */
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
            *seqNum = *seqNum + 1;

            if (recvfrom(sockfd, recvPacket, sizeof(struct Packet), 0, senderAddr, &addrLen) < 0) {
                perror("Error: Failed to receive third packet in three-way handshake.");
                exit(EXIT_FAILURE);
            }

            if (recvPacket->ackBit == 1 && recvPacket->ackNum == sendPacket->seqNum) {
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

    free(recvPacket);
    free(sendPacket);
}

/**
 * @brief disconnectSocket is a function that terminates a connection with a sender.
 * 
 * @param sockfd        The file descriptor of the socket
 * @param senderAddr    The address of the sender
 * @param addrLen       The length of the sender address
 * @param seqNum        The sequence number of the receiver
 */
void disconnectSocket(int sockfd, struct sockaddr *senderAddr, socklen_t addrLen, int *seqNum) {
    struct Packet *recvPacket;
    struct Packet *sendPacket;
    int connectionFinished = 0;

    recvPacket = malloc(sizeof(struct Packet));
    sendPacket = malloc(sizeof(struct Packet));

    // Begin disconnection
    while (!connectionFinished) {
        // Check that a second FIN packet is received
        if (recvfrom(sockfd, recvPacket, sizeof(struct Packet), 0, senderAddr, &addrLen) < 0) {
            perror("Error: Failed to receive fourth packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        sendPacket->seqNum = *seqNum;
        sendPacket->ackBit = 1;
        sendPacket->ackNum = recvPacket->seqNum;
        
        // Send an acknowledgment for the second FIN packet
        if (sendto(sockfd, sendPacket, sizeof(struct Packet), 0, senderAddr, addrLen) < 0) {
            perror("Error: Failed to send second packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        *seqNum = *seqNum + 1;

        sendPacket->seqNum = *seqNum;
        sendPacket->ackBit = 1;
        sendPacket->ackNum = recvPacket->seqNum;
        sendPacket->finBit = 1;

        // Send a FIN packet to the sender
        if (sendto(sockfd, sendPacket, sizeof(struct Packet), 0, senderAddr, addrLen) < 0) {
            perror("Error: Failed to send third packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        // Check that the ACK for the receiver FIN is received, and finish the disconnection
        if (recvfrom(sockfd, recvPacket, sizeof(struct Packet), 0, senderAddr, &addrLen) < 0) {
            perror("Error: Failed to receive fourth packet during disconnect.");
            exit(EXIT_FAILURE);
        }
        else {
            connectionFinished = 1;
        }
    }

    free(recvPacket);
    free(sendPacket);
}

/**
 * @brief receiveData is a function that receives data from a sender.
 * 
 * @param sockfd            The file descriptor of the socket
 * @param senderAddr        The address of the sender
 * @param addrLen           The length of the sender address
 * @param seqNum            The sequence number of the receiver
 * @param destinationFile   The file to write the data to
 * @param writeRate         The rate at which to write data to the file
 */
void receiveData(int sockfd, struct sockaddr *senderAddr, socklen_t addrLen, int *seqNum, char* destinationFile, unsigned long long int writeRate) {
    struct Packet *recvPacket;
    struct Packet *sendPacket;
    int initDisconnect = 0;
    int expectedAckNum = 1;

    recvPacket = malloc(sizeof(struct Packet));
    sendPacket = malloc(sizeof(struct Packet));
    
    // Open the file
    FILE* file = fopen(destinationFile, "w+");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to open file %s\n", destinationFile);
        exit(EXIT_FAILURE);
    }

    while (!initDisconnect) {
        // Check for incoming data
        if (recvfrom(sockfd, recvPacket, sizeof(struct Packet), 0, senderAddr, &addrLen) < 0) {
            perror("Error: Failed to receive packet during data transfer.");
            exit(EXIT_FAILURE);
        }

        // If a packet with the FIN bit is received, begin disconnect
        if (recvPacket->finBit == 1) {
            initDisconnect = 1;
            expectedAckNum = recvPacket->seqNum + 1;
        }
        else {
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

            fwrite(recvPacket->data, 1, recvPacket->dataSize, file);
            fflush(file);
        }
    }

    fclose(file);
    free(recvPacket);
    free(sendPacket);
}

/**
 * @brief rrecv is a function that receives data from a sender.
 * 
 * @param myUDPport         The port to receive data on
 * @param destinationFile   The file to write the data to
 * @param writeRate         The rate at which to write data to the file
 */
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
    receiveData(sockfd, (struct sockaddr *)&senderAddr, addrLen, seqNum, destinationFile, writeRate);

    // Disconnect from sender
    disconnectSocket(sockfd, (struct sockaddr *)&senderAddr, addrLen, seqNum);

    // Close the socket and free memory
    close(sockfd);
    free(seqNum);
    free(ackNum);
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