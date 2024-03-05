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

#define MAX_DATA_SIZE 1024
#define MAX_WINDOW_SIZE 10
#define TIMOUT_SEC 1 
#define SEQ_NUM 1
#define MSS 1460
#define DECREASE_RATE 2
#define INCREASE_RATE 1
#define SECOND 1
#define FIN_BIT_SENT 1

pthread_mutex_t threadLock;
int continueThread = 1; 
int sendRate = 1;
int startDisconnect = 0;
int globalSeqNum = SEQ_NUM;

struct SendThreadArgs {
    FILE* file;
    int currentSeq;
    int sockfd;
    struct sockaddr_in *receiverAddr;
    unsigned long long int* bytesTransferred;
    unsigned long long int bytesToTransfer;
    socklen_t addrLen;
};

/**
 * @brief *sendPacketsContinuously is a function that sends data to the receiver until told to join the main 
 *         thread if duplicate acks or packet loss occurs.
 * 
 * @param arg   SendThreadArgs with values ready to send to receiver.
 */
void *sendPacketsContinuously(void *arg) {
    struct Packet currPacket;
    struct SendThreadArgs *packetArgs = (struct SendThreadArgs *) arg;
    long int currentLine = (packetArgs->currentSeq - 1) * MSS;

    if (fseek(packetArgs->file, currentLine, SEEK_SET) != 0) {
        perror("Error - wrong seek line");
    }

    while ((packetArgs->bytesTransferred <= packetArgs->bytesToTransfer) && (!feof(packetArgs->file)) && (continueThread)) {
        
        currPacket.seqNum = packetArgs->currentSeq;
        currPacket.ackNum = 0;
        currPacket.ackBit = 0;
        currPacket.finBit = 0;
        currPacket.dataSize = fread(&currPacket.data, 1, MSS, packetArgs->file);

        if (currPacket.dataSize == 0) {
            currPacket.finBit = 1;
        }

        if (sendto(packetArgs->sockfd, &currPacket, sizeof(currPacket), 0, packetArgs->receiverAddr, packetArgs->addrLen) < 0) {
            perror("Error: Failed to send first packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        sleep(SECOND / sendRate);
        packetArgs->currentSeq++;
        globalSeqNum++;
        sendRate = sendRate + INCREASE_RATE;
    }

    if (feof(packetArgs->file) || (packetArgs->bytesTransferred >= packetArgs->bytesToTransfer)) {
        currPacket.seqNum = packetArgs->currentSeq;
        currPacket.ackNum = 0;
        currPacket.ackBit = 0;
        currPacket.finBit = 1;
        if (sendto(packetArgs->sockfd, &currPacket, sizeof(currPacket), 0, packetArgs->receiverAddr, packetArgs->addrLen) < 0) {
            perror("Error: Failed to send first packet during disconnect.");
            exit(EXIT_FAILURE);
        }
        currPacket.seqNum++;
        globalSeqNum++;
        if (sendto(packetArgs->sockfd, &currPacket, sizeof(currPacket), 0, packetArgs->receiverAddr, packetArgs->addrLen) < 0) {
            perror("Error: Failed to send first packet during disconnect.");
            exit(EXIT_FAILURE);
        }
        startDisconnect = 1;
    }

}

/**
 * @brief sendPacket function to send packet to receiver.
 * 
 * @param sockfd            The file descriptor of the socket.
 * @param receiverAddr     The address of the receiver.           
 * @param addrLen           The size of the receiver address.
 */
int sendPacket(int sockfd, struct Packet packet, struct sockaddr_in receiverAddr, socklen_t addrLen) {
    ssize_t bytesSent = sendto(sockfd, &packet, sizeof(packet), 0,
                                (struct sockaddr *)&receiverAddr, addrLen);
    if (bytesSent == -1) {
        perror("sendto");
        return -1;
    }
    return 0;
}

/**
 * @brief connectToReceiver is a function that initializes a three-way handshake connection with a receiver.
 * 
 * @param sockfd            The file descriptor of the socket.
 * @param sendingPacket    The packet to send to the receiver.
 * @param receivePacket    The packet to receive from the receiver.
 * @param receiverAddr     The address of the receiver.           
 * @param addrLen           The size of the receiver address.
 */
void connectToReceiver(int sockfd, struct Packet sendingPacket, struct Packet receivePacket, 
             struct sockaddr_in receiverAddr, socklen_t addrLen) {
    
    int connectionFinished = 0;
    int currentSeqNum = SEQ_NUM;

    while(!connectionFinished) {
        sendingPacket.seqNum = currentSeqNum;
        sendingPacket.synBit = 1;

        if (sendPacket(sockfd, sendingPacket, receiverAddr, addrLen) < 0) {
            perror("Error: Failed to send first packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        if (recvfrom(sockfd, &receivePacket, sizeof(receivePacket), 0, (struct sockaddr *)&receiverAddr, &addrLen) < 0) {
            perror("Error: Failed to receive first packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        if (receivePacket.ackBit != sendingPacket.synBit) {
            printf(stderr, "Error: Invalid sequence number\n");
        } else {
            currentSeqNum++;
            sendingPacket.ackNum = receivePacket.seqNum;
            sendingPacket.ackBit = receivePacket.synBit;
            if (sendPacket(sockfd, sendingPacket, receiverAddr, addrLen) < 0) {
                perror("Error: Failed to send third packet during disconnect.");
                exit(EXIT_FAILURE);
            } else {
                connectionFinished = 1;
            }
        }
    }
}

/**
 * @brief disconnectFromReceiver is a function that terminates a connection with a receiver.
 * 
 * @param sockfd            The file descriptor of the socket.
 * @param sendingPacket    The packet to send to the receiver.
 * @param receivePacket    The packet to receive from the receiver.
 * @param receiverAddr     The address of the receiver.    
 * @param currentSeqNum     The current sequence number.       
 * @param addrLen           The size of the receiver address.
 */
void disconnectFromReceiver(int sockfd, struct Packet sendingPacket, struct Packet receivePacket, 
                struct sockaddr_in receiverAddr, int currentSeqNum, socklen_t addrLen) {
    int connectionFinished = 0;

    while(!connectionFinished) {
        sendingPacket.seqNum = globalSeqNum;
        sendingPacket.finBit = 1;
        sendingPacket.ackBit = 0;

        // Send FIN
        if (sendPacket(sockfd, sendingPacket, receiverAddr, addrLen) < 0) {
            perror("Error: Failed to send first packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        // Receive ACKd FIN
        if (recvfrom(sockfd, &receivePacket, sizeof(receivePacket), 0, (struct sockaddr *)&receiverAddr, &addrLen) < 0) {
            perror("Error: Failed to receive first packet during disconnect.");
            exit(EXIT_FAILURE);
        }   

        if (receivePacket.ackBit != sendingPacket.finBit) {
            printf(stderr, "Error: Invalid sequence number\n");
        } else {
            globalSeqNum++;
            sendingPacket.ackNum = receivePacket.seqNum;
            sendingPacket.ackBit = 1;
            sendingPacket.seqNum = globalSeqNum;
            if (sendPacket(sockfd, sendingPacket, receiverAddr, addrLen) < 0) {
                perror("Error: Failed to send third packet during disconnect.");
                exit(EXIT_FAILURE);
            } else {
                connectionFinished = 1;
            }
        }
    }
}

/**
 * @brief rsend is a function that sends data to the receiver address.
 * 
 * @param hostname          Host address of the receiver.   
 * @param hostUDPport       The port to send data on.
 * @param filename          The filename of the file.
 * @param bytesToTransfer   The number of bytes to send to the receiver.    
 */
void rsend(char* hostname, 
            unsigned short int hostUDPport, 
            char* filename, 
            unsigned long long int bytesToTransfer) 
{
    int sockfd;
    struct sockaddr_in receiverAddr;
    char buffer[MAX_DATA_SIZE];
    struct Packet senderPacket;
    struct Packet receivePacket; 
    unsigned int currentSeqNum = SEQ_NUM;
    unsigned int currentAckNum;
    pthread_t senderThreadId;
    unsigned long long int* bytesTransferred = 0;
    unsigned int cwnd = 1;
    struct SendThreadArgs sendArgs;
    socklen_t addrLen;

    // Create UDP Socket 
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Initialize receiver address 
    memset(&receiverAddr, 0, sizeof(receiverAddr));
    receiverAddr.sin_family = AF_INET;
    receiverAddr.sin_port = htons(hostUDPport);
    inet_pton(AF_INET, hostname, &receiverAddr.sin_addr);

    addrLen = sizeof(receiverAddr);
    connectToReceiver(sockfd, senderPacket, receivePacket, receiverAddr, addrLen);

    // Open the file
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to open file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    sendArgs.file = file;
    sendArgs.currentSeq = 1;
    sendArgs.sockfd = sockfd;
    sendArgs.receiverAddr = &receiverAddr;
    sendArgs.bytesTransferred = bytesTransferred;
    sendArgs.bytesToTransfer = bytesToTransfer;
    sendArgs.addrLen = addrLen;

    if (pthread_mutex_init(&threadLock, NULL) != 0) { 
        printf("\n mutex init has failed\n"); 
        exit(EXIT_FAILURE); 
    } 

    if (pthread_create(&senderThreadId, NULL, sendPacketsContinuously, (void *)&sendArgs)) {
        perror("Error: failed to create transmission thread");
        exit(EXIT_FAILURE);
    }

    globalSeqNum = 1;

    int disconnect = 0;
    while (!disconnect) {
        struct Packet ackPacket;

        if (startDisconnect) {
            disconnect = 1;
            break;
        }

        if (recvfrom(sockfd, &ackPacket, sizeof(ackPacket), 0, (struct sockaddr *)&receiverAddr, &addrLen) < 0) {
            perror("Error: Failed to receive ACK packet - Handling Acks\n");
            exit(EXIT_FAILURE);
        }

        if ((ackPacket.ackNum >= currentSeqNum)) {
            globalSeqNum = ackPacket.ackNum + 1;
        } else {
            // Finish sending data because of packet loss and end thread and join it back
            pthread_mutex_lock(&threadLock);
            continueThread = 0;
            pthread_mutex_unlock(&threadLock);
            if (pthread_join(senderThreadId, NULL) != 0) {
                perror("pthread_join");
                exit(EXIT_FAILURE);
            }

            sendRate = sendRate / DECREASE_RATE;

            // sendArgs added to last send that has been acknowledged
            sendArgs.file = file;
            sendArgs.currentSeq = globalSeqNum;
            sendArgs.sockfd = sockfd;
            sendArgs.receiverAddr = &receiverAddr;
            sendArgs.bytesTransferred = bytesTransferred;
            sendArgs.bytesToTransfer = bytesToTransfer;
            sendArgs.addrLen = addrLen;

            // start sending again 
            pthread_mutex_lock(&threadLock);
            continueThread = 1;
            pthread_mutex_unlock(&threadLock);
            if (pthread_create(&senderThreadId, NULL, sendPacketsContinuously, (void *)&sendArgs)) {
                perror("Error: failed to create transmission thread");
                exit(EXIT_FAILURE);
            }
        }
   }
   // Start disconnecting from receiver
   disconnectFromReceiver(sockfd, senderPacket, receivePacket, receiverAddr, globalSeqNum, addrLen);
}


int main(int argc, char** argv) {
    // This is a skeleton of a main function.
    // You should implement this function more completely
    // so that one can invoke the file transfer from the
    // command line.
    int hostUDPport;
    unsigned long long int bytesToTransfer;
    char* hostname = NULL;
    char* filename = NULL;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    } 

    hostname = argv[1];
    hostUDPport = (unsigned short int) atoi(argv[2]);
    filename = argv[3];
    bytesToTransfer = atoll(argv[4]);

    rsend(hostname, hostUDPport, filename, bytesToTransfer);

    return (EXIT_SUCCESS);
}
