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
#define RECEIVER_SEQ_NUM 1000

struct Packet {
    unsigned int seq_num;
    unsigned int ack_num;
    int rwnd;
    size_t data_size;
    char data[MSS];
    int FIN;
};

struct PacketQueue {
    struct Packet *packets;
    int front;
    int rear;
    int size;
    int capacity;
    int packet_loss;
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

// Function to check the amount of empty space in the packet queue
int packet_queue_empty_space(struct PacketQueue *queue) {
    return (queue->capacity - queue->size);
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

// Function to enqueue a packet into the front of the packet queue
void enqueue_packet_from_front(struct PacketQueue *queue, struct Packet packet) {
    if (is_packet_queue_full(queue)) {
        printf("Packet queue is full. Cannot enqueue packet.\n");
        return;
    }
    queue->front = (queue->front - 1) % queue->capacity;
    queue->packets[queue->front] = packet;
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

// Function to free the memory allocated for the packet queue
void free_packet_queue(struct PacketQueue *queue) {
    free(queue->packets);
    queue->front = 0;
    queue->rear = -1;
    queue->size = 0;
    queue->capacity = 0;
}

// Function to send ack
unsigned int send_ack(int sockfd, struct sockaddr_in sender_addr, unsigned int seq_num, unsigned int ack_num, int rwnd) {
    struct Packet packet;
    packet.seq_num = seq_num;
    packet.ack_num = ack_num;
    packet.rwnd = rwnd;
    ssize_t bytes_sent = sendto(sockfd, &packet, sizeof(packet), 0,
                                (struct sockaddr *)&sender_addr, sizeof(sender_addr));
    if (bytes_sent == -1) {
        perror("sendto");
        return -1;
    }
    return 0;
}

struct WriteArgs {
    FILE *file;
    struct PacketQueue *packet_queue;
    unsigned long long int writeRate;
};

void *write_to_file(void *args) {
    struct WriteArgs *write_args = (struct WriteArgs *)args;
    while (1) {
        // Check packetQueue for data or packet loss
        if (is_packet_queue_empty(write_args->packet_queue) || write_args->packet_queue->packet_loss == 1) {
            continue;
        }

        // Dequeue a packet from the packet queue
        struct Packet packet = dequeue_packet(write_args->packet_queue);

        // Check if the write rate is 0 or greater than the packet size, if so write the entire packet to the file
        if (write_args->writeRate == 0 || write_args->writeRate >= packet.data_size) {
            fwrite(packet.data, 1, packet.data_size, write_args->file);
            fflush(write_args->file);
            sleep(1);
        }
        else {
            // Otherwise, write the packet to the file in chunks
            while (packet.data_size > 0) {
                // Check the remaining data size and write rate to determine how many bytes to write
                size_t bytes_to_write = (write_args->writeRate < packet.data_size) ? write_args->writeRate : packet.data_size;
                fwrite(packet.data, 1, bytes_to_write, write_args->file);
                fflush(write_args->file);
                packet.data_size -= bytes_to_write;

                // Move the remaining data to the beginning of the buffer
                memmove(packet.data, packet.data + write_args->writeRate, packet.data_size);
                sleep(1);
            }
        }
    }
}

struct RecvArgs {
    int sockfd;
    struct sockaddr_in sender_addr;
    unsigned int seq_num;
    unsigned int ack_num, expectedAckNum;
    struct PacketQueue *packet_queue;
};

// This thread receives data from the sender and enqueues it into the packet queue. 
// This thread should signal for congestion control if the packet queue is full.
void *receive_data(void *args) {
    struct RecvArgs *recv_args = (struct RecvArgs *)args;
    struct Packet packet;
    ssize_t bytesRead;
    socklen_t addr_size = sizeof(recv_args->sender_addr);

    while (1) {
        // Receive data from sender
        bytesRead = recvfrom(recv_args->sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&recv_args->sender_addr, &addr_size);
        if (bytesRead == -1) {
            perror("recvfrom");
            exit(EXIT_FAILURE);
        }
        recv_args->ack_num = packet.seq_num;

        // Send ack
        send_ack(recv_args->sockfd, recv_args->sender_addr, recv_args->seq_num, recv_args->ack_num, packet_queue_empty_space(recv_args->packet_queue));
        recv_args->seq_num++;

        // If the packet is a FIN packet, break the loop
        if (packet.FIN == 1) {
            break;
        }

        // If the packet queue is full, wait for it to become available
        while (is_packet_queue_full(recv_args->packet_queue)) {
            send_ack(recv_args->sockfd, recv_args->sender_addr, recv_args->seq_num, recv_args->ack_num, packet_queue_empty_space(recv_args->packet_queue));
            recv_args->seq_num++;
            continue;
        }

        // If the packet is not the expected sequence number, send acks until it is
        while (packet.ack_num != recv_args->expectedAckNum) {
            send_ack(recv_args->sockfd, recv_args->sender_addr, recv_args->seq_num, recv_args->ack_num, packet_queue_empty_space(recv_args->packet_queue));
            recv_args->seq_num++;
        }

        // Enqueue the packet into the packet queue
        enqueue_packet(recv_args->packet_queue, packet);
    }
}

void rrecv(unsigned short int myUDPport,
           char* destinationFile, 
           unsigned long long int writeRate) {
    int sockfd;
    struct sockaddr_in receiver_addr, sender_addr;
    ssize_t bytesRead;
    socklen_t addr_size;
    struct Packet packet;
    unsigned int currentSeqNum = RECEIVER_SEQ_NUM;
    unsigned int currentAckNum;
    pthread_t write_thread_id;
    pthread_t send_ack_thread_id;
    struct PacketQueue packet_queue;

    // Initialize the packet queue
    init_packet_queue(&packet_queue, 16);

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

    // Now send ack
    send_ack(sockfd, sender_addr, currentSeqNum, currentAckNum, packet_queue_empty_space(&packet_queue));

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
    struct WriteArgs args;
    args.file = file;
    args.packet_queue = &packet_queue;
    args.writeRate = writeRate;
    pthread_create(&write_thread_id, NULL, &write_to_file, (void *)&args);

    // Create a thread to receive data from the sender
    struct RecvArgs recv_args;
    recv_args.sockfd = sockfd;
    recv_args.sender_addr = sender_addr;
    recv_args.seq_num = currentSeqNum;  
    recv_args.ack_num = currentAckNum;
    recv_args.packet_queue = &packet_queue;
    pthread_create(&send_ack_thread_id, NULL, &receive_data, (void *)&recv_args);

    pthread_join(write_thread_id, NULL);
    pthread_join(send_ack_thread_id, NULL);

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
