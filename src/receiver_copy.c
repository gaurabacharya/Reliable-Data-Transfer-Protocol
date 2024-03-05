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
// #include "./include/packetQueue.c"
// #include "./include/writeThreadArgs.h"

struct WriteThreadArgs {
    FILE                    *file;
    struct PacketQueue      *packetQueue;
    unsigned long long int  writeRate;
};

struct PacketQueue {
    struct Packet   packets[16];
    int             front;
    int             rear;
    int             size;
    int             capacity;
    int             packetLoss;
};

// Function to initialize a packet queue
void initPacketQueue(struct PacketQueue *queue) {
    queue = malloc(sizeof(struct PacketQueue));
    queue->front = 0;
    queue->rear = -1;
    queue->size = 0;
    queue->capacity = 16;
    queue->packetLoss = 0;
    for (int i = 0; i < 16; i++) {
        queue->packets[i] = (struct Packet){0};
    }
}

// Function to check if the packet queue is empty
int isPacketQueueEmpty(struct PacketQueue *queue) {
    return (queue->size == 0);
}

// Function to check if the packet queue is full
int isPacketQueueFull(struct PacketQueue *queue) {
    return (queue->size == queue->capacity);
}

// Function to check the amount of empty space in the packet queue
int getPacketQueueEmptySpace(struct PacketQueue *queue) {
    return (queue->capacity - queue->size);
}

// Function to enqueue a packet into the packet queue
void enqueuePacket(struct PacketQueue *queue, struct Packet packet) {
    if (isPacketQueueFull(queue)) {
        printf("Packet queue is full. Cannot enqueue packet.\n");
        return;
    }
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->packets[queue->rear] = packet;
    queue->size++;
}

// Function to enqueue a packet into the front of the packet queue
void enqueue_packet_from_front(struct PacketQueue *queue, struct Packet packet) {
    if (isPacketQueueFull(queue)) {
        printf("Packet queue is full. Cannot enqueue packet.\n");
        return;
    }
    queue->front = (queue->front - 1 + queue->capacity) % queue->capacity;
    queue->packets[queue->front] = packet;
    queue->size++;
}

// Function to dequeue a packet from the packet queue
struct Packet dequeuePacket(struct PacketQueue *queue) {
    if (isPacketQueueEmpty(queue)) {
        printf("Packet queue is empty. Cannot dequeue packet.\n");
        struct Packet emptyPacket;
        emptyPacket.seqNum = 0;
        emptyPacket.ackNum = 0;
        memset(emptyPacket.data, 0, sizeof(emptyPacket.data));
        return emptyPacket;
    }
    printf("Getting front packet.\n");
    struct Packet packet = queue->packets[queue->front];
    printf("Updating front.\n");
    queue->front = (queue->front + 1) % queue->capacity;
    printf("decreasing size.\n");
    queue->size--;
    return packet;
}

// Function to get the front packet of the packet queue
struct Packet frontPacket(struct PacketQueue *queue) {
    if (isPacketQueueEmpty(queue)) {
        printf("Packet queue is empty. Cannot get front packet.\n");
        struct Packet emptyPacket;
        emptyPacket.seqNum = 0;
        emptyPacket.ackNum = 0;
        memset(emptyPacket.data, 0, sizeof(emptyPacket.data));
        return emptyPacket;
    }
    return queue->packets[queue->front];
}

// Function to free the memory allocated for the packet queue
void free_packet_queue(struct PacketQueue *queue) {
    free(queue->packets);
    queue->front = 0;
    queue->rear = -1;
    queue->size = 0;
    queue->capacity = 0;
}

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
 * @param ackNum        The acknowledgment number of the receiver
 */
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

    free(recvPacket);
    free(sendPacket);
}

/**
 * @brief writeData is a function that writes data to a file.
 * 
 * @param args  The arguments for the write thread
 */
void *writeData(void *args) {
    struct WriteThreadArgs *writeArgs = (struct WriteThreadArgs *)args;
    int finished = 0;

    while (!finished) {
        printf("Checking packet queue.\n");
        if (isPacketQueueEmpty(writeArgs->packetQueue)) {
            printf("Packet queue is empty.\n");
            continue;
        }

        printf("Packet queue is not empty.\n");
        // if (writeArgs->packetQueue->packetLoss) {
        //     printf("Packet loss value is %d\n", writeArgs->packetQueue->packetLoss);
        //     printf("Packet loss is detected.\n");
        //     continue;
        // }
        // Check packetQueue for data or packet loss
        // if (isPacketQueueEmpty(writeArgs->packetQueue) || writeArgs->packetQueue->packetLoss) {
        //     // printf("Packet queue is empty or packet loss occurred.\n");
        //     continue;
        // }

        // if (writeArgs->packetQueue->size > 0) {
        //     printf("Packet queue size is %d\n", writeArgs->packetQueue->size);
        //     continue;
        // }

        // Dequeue a packet from the packet queue
        struct Packet packet = dequeuePacket(writeArgs->packetQueue);

        printf("Dequeued packet with sequence number %d\n", packet.seqNum);

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

    printf("Finished writing to file.\n");
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
 * @return int              The sequence number of the last packet received
 */
int receiveData(int sockfd, struct sockaddr *senderAddr, socklen_t addrLen, int *seqNum, char* destinationFile, unsigned long long int writeRate) {
    struct Packet *recvPacket;
    struct Packet *sendPacket;
    struct PacketQueue *packetQueue;
    struct WriteThreadArgs args;
    int initDisconnect = 0;
    int expectedAckNum = 1;
    pthread_t writeThread;

    recvPacket = malloc(sizeof(struct Packet));
    sendPacket = malloc(sizeof(struct Packet));
    // args = malloc(sizeof(struct WriteThreadArgs));

    initPacketQueue(packetQueue);
    
    // Open the file
    FILE* file = fopen(destinationFile, "w+");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to open file %s\n", destinationFile);
        exit(EXIT_FAILURE);
    }

    printf("About to assign values to arg.\n");
    // Create a thread to write to the file
    // args->file = file;
    // args->packetQueue = packetQueue;
    // printf("args->packetQueue->size: %d\n", args->packetQueue->size);
    // args->writeRate = writeRate;
    args.file = file;
    args.packetQueue = packetQueue;
    printf("args->packetQueue->size: %d\n", args.packetQueue->size);
    args.writeRate = writeRate;
    pthread_create(&writeThread, NULL, &writeData, (void *)&args);

    printf("Started writing thread.\n");

    while (!initDisconnect) {
        printf("About to receive packet.\n");
        if (recvfrom(sockfd, recvPacket, sizeof(struct Packet), 0, senderAddr, &addrLen) < 0) {
            perror("Error: Failed to receive packet during data transfer.");
            exit(EXIT_FAILURE);
        }

        printf("Received packet with sequence number %d\n", recvPacket->seqNum);

        if (recvPacket->finBit == 1) {
            initDisconnect = 1;
            expectedAckNum = recvPacket->seqNum + 1;
            printf("Received FIN bit. Preparing to disconnect.\n");
        }
        else {
            // Check for congestion
            while (isPacketQueueFull(packetQueue)) {
                printf("Packet queue is full. Waiting for space to open up.\n");
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
                printf("Packet loss detected. Waiting for retransmission.\n");
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
        printf("Enqueued packet with sequence number %d\n", recvPacket->seqNum);
        printf("Queue size: %d\n", packetQueue->size);
    }

    pthread_join(writeThread, NULL);
    fclose(file);
    free(recvPacket);
    free(sendPacket);
    // free(args);
    free(packetQueue);
    return recvPacket->seqNum;
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
    *ackNum = receiveData(sockfd, (struct sockaddr *)&senderAddr, addrLen, seqNum, destinationFile, writeRate);

    // Disconnect from sender
    disconnectSocket(sockfd, (struct sockaddr *)&senderAddr, addrLen, seqNum, ackNum);

    // Close the socket
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
