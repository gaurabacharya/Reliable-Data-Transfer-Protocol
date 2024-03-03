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

#define MSS 1460
#define MAX_DATA_SIZE 1024
#define RECEIVER_SEQ_NUM 1000

struct Packet {
    unsigned int seq_num;
    unsigned int ack_num;
    char data[MSS];
};

struct PacketQueue {
    struct Packet *packets;
    int front;
    int rear;
    int size;
    int capacity;
};

// Function to initialize a packet queue
void init_packet_queue(struct PacketQueue *queue, int capacity) {
    queue->packets = (struct Packet *)malloc(capacity * sizeof(struct Packet));
    queue->front = 0;
    queue->rear = -1;
    queue->size = 0;
    queue->capacity = capacity;
}

// Function to check if the packet queue is empty
int is_packet_queue_empty(struct PacketQueue *queue) {
    return (queue->size == 0);
}

// Function to check if the packet queue is full
int is_packet_queue_full(struct PacketQueue *queue) {
    return (queue->size == queue->capacity);
}

// Function to enqueue a packet into the packet queue
void enqueue_packet(struct PacketQueue *queue, struct Packet packet) {
    if (is_packet_queue_full(queue)) {
        printf("Packet queue is full. Cannot enqueue packet.\n");
        return;
    }
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->packets[queue->rear] = packet;
    queue->size++;
}

// Function to dequeue a packet from the packet queue
struct Packet dequeue_packet(struct PacketQueue *queue) {
    if (is_packet_queue_empty(queue)) {
        printf("Packet queue is empty. Cannot dequeue packet.\n");
        struct Packet empty_packet;
        empty_packet.seq_num = 0;
        empty_packet.ack_num = 0;
        memset(empty_packet.data, 0, sizeof(empty_packet.data));
        return empty_packet;
    }
    struct Packet packet = queue->packets[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    return packet;
}

// Function to get the front packet of the packet queue
struct Packet front_packet(struct PacketQueue *queue) {
    if (is_packet_queue_empty(queue)) {
        printf("Packet queue is empty. Cannot get front packet.\n");
        struct Packet empty_packet;
        empty_packet.seq_num = 0;
        empty_packet.ack_num = 0;
        memset(empty_packet.data, 0, sizeof(empty_packet.data));
        return empty_packet;
    }
    return queue->packets[queue->front];
}

// Function to get the rear packet of the packet queue
struct Packet rear_packet(struct PacketQueue *queue) {
    if (is_packet_queue_empty(queue)) {
        printf("Packet queue is empty. Cannot get rear packet.\n");
        struct Packet empty_packet;
        empty_packet.seq_num = 0;
        empty_packet.ack_num = 0;
        memset(empty_packet.data, 0, sizeof(empty_packet.data));
        return empty_packet;
    }
    return queue->packets[queue->rear];
}

// Function to free the memory allocated for the packet queue
void free_packet_queue(struct PacketQueue *queue) {
    free(queue->packets);
    queue->front = 0;
    queue->rear = -1;
    queue->size = 0;
    queue->capacity = 0;
}

struct Args {
    FILE *file;
    char buffer[8*MSS];
    unsigned long long int writeRate;
};

// Function to send ack
unsigned int send_ack(int sockfd, struct sockaddr_in sender_addr, unsigned int seq_num, unsigned int ack_num) {
    struct Packet packet;
    packet.seq_num = seq_num;
    packet.ack_num = ack_num;
    ssize_t bytes_sent = sendto(sockfd, &packet, sizeof(packet), 0,
                                (struct sockaddr *)&sender_addr, sizeof(sender_addr));
    if (bytes_sent == -1) {
        perror("sendto");
        return -1;
    }
    return 0;
}

void *write_to_file(void *args) {
    struct Args *write_args = (struct Args *)args;
    while (1) {
        // Write data to file
        size_t buffer_size = sizeof(write_args->buffer);
        size_t bytes_to_write = (write_args->writeRate < buffer_size) ? write_args->writeRate : buffer_size;
        fwrite(write_args->buffer, 1, bytes_to_write, write_args->file);
        fflush(write_args->file);
        
        // Move the remaining data to the beginning of the buffer
        size_t remaining_bytes = buffer_size - bytes_to_write;
        if (remaining_bytes > 0) {
            memmove(write_args->buffer, write_args->buffer + bytes_to_write, remaining_bytes);
        }
        
        // Zero out the remaining part of the buffer
        memset(write_args->buffer + remaining_bytes, 0, bytes_to_write);
        sleep(1);
    }
}

void rrecv(unsigned short int myUDPport,
           char* destinationFile, 
           unsigned long long int writeRate) {
    int sockfd;
    struct sockaddr_in receiver_addr, sender_addr;
    char buffer[8*MSS];
    ssize_t bytesRead;
    socklen_t addr_size;
    struct Packet packet;
    unsigned int currentSeqNum = RECEIVER_SEQ_NUM;
    unsigned int currentAckNum;
    pthread_t write_thread_id;
    pthread_t ack_thread_id;
    struct PacketQueue packet_queue;

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Set up the server address
    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(myUDPport);
    receiver_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the socket
    if (bind(sockfd, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    addr_size = sizeof(sender_addr);

    // Wait for a connection request
    recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&sender_addr, &addr_size);
    currentAckNum = packet.seq_num;

    // Now send ack with write rate to sender
    send_ack(sockfd, sender_addr, currentSeqNum, currentAckNum);

    // Wait for ack from sender
    bytesRead = recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&sender_addr, &addr_size);
    if ((bytesRead == -1) || (packet.ack_num != currentSeqNum)) {
        fprintf(stderr, "Error: Invalid sequence number\n");
        exit(EXIT_FAILURE);
    }
    currentSeqNum++;
    currentAckNum = packet.seq_num + 1;

    // Open the file
    FILE* file = fopen(destinationFile, "w+");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to open file %s\n", destinationFile);
        exit(EXIT_FAILURE);
    }

    // Create a thread to write to the file
    struct Args Args;
    Args.file = file;
    memcpy(Args.buffer, buffer, sizeof(buffer));
    Args.writeRate = writeRate;

    printf("Creating thread to write to file\n");
    pthread_create(&write_thread_id, NULL, &write_to_file, (void *)&Args);

    init_packet_queue(&packet_queue, 16);
    // MAKE THIS WORK

    while (1) {
        // Receive data from sender
        struct Packet packet;
        ssize_t bytesRead = recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&sender_addr, &addr_size);
        if (bytesRead == -1) {
            perror("recvfrom");
            exit(EXIT_FAILURE);
        }

        // Check for packet loss
        if (packet.seq_num != currentAckNum + 1) {
            send_ack(sockfd, sender_addr, currentSeqNum, currentAckNum);
        }
        else {
            currentAckNum = packet.seq_num;
            send_ack(sockfd, sender_addr, currentSeqNum, currentAckNum);
            strcat(buffer, packet.data);
        }
    }

    fclose(file);
    close(sockfd);
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
