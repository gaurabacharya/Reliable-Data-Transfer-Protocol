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

pthread_mutex_t thread_lock;
int continue_thread = 1; 
int send_rate = 1;

struct CheckAckArgs {
    int *sockfd;
    struct sockaddr_in *receiver_addr;
    unsigned int *seq_num;
    unsigned int *ack_num;
};

struct SendThreadArgs {
    FILE* file;
    int currentSeq;
    int sockfd;
    struct sockaddr_in *receiver_addr;
    unsigned long long int* bytesTransferred;
    unsigned long long int bytesToTransfer;
};

void *send_packets_continuously(void *arg) {
    struct Packet currPacket;
    struct SendThreadArgs *packet_args = (struct SendThreadArgs *) arg;
    long int currentLine = packet_args->currentSeq * MSS;

    if (fseek(packet_args->file, currentLine, SEEK_SET) != 0) {
        perror("Error - wrong seek line");
    }

    while (packet_args->bytesTransferred <= packet_args->bytesToTransfer && !feof(packet_args->file) && continue_thread) {
        currPacket.seqNum = packet_args->currentSeq;
        currPacket.ackNum = 0;
        currPacket.ackBit = 0;
        currPacket.finBit = 0;
        currPacket.dataSize = fread(&currPacket.data, 1, MSS, packet_args->file);

        if (feof(packet_args->file) || currPacket.dataSize == 0) {
            currPacket.finBit = 1;
        }

        if (sendto(packet_args->sockfd, &currPacket, sizeof(&currPacket), 0, packet_args->receiver_addr, sizeof(packet_args->receiver_addr)) < 0) {
            perror("Error: Failed to send first packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        sleep(SECOND / send_rate);
        packet_args->currentSeq++;
        send_rate = send_rate + INCREASE_RATE;
    }
}

// Function to send packet
int send_packet(int sockfd, struct Packet packet, struct sockaddr_in receiver_addr) {
    ssize_t bytes_sent = sendto(sockfd, &packet, sizeof(packet), 0,
                                (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
    if (bytes_sent == -1) {
        perror("sendto");
        return -1;
    }
    return 0;
}

void connect_to_receiver(int sockfd, struct Packet sending_packet, struct Packet receive_packet, 
             struct sockaddr_in receiver_addr) {
    
    int connection_finished = 0;
    int currentSeqNum = SEQ_NUM;

    while(!connection_finished) {
        sending_packet.seqNum = currentSeqNum;
        sending_packet.synBit = 1;

        if (send_packet(sockfd, sending_packet, receiver_addr) < 0) {
            perror("Error: Failed to send first packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        if (recvfrom(sockfd, &receive_packet, sizeof(receive_packet), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr)) < 0) {
            perror("Error: Failed to receive first packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        if (receive_packet.ackBit != sending_packet.synBit) {
            printf(stderr, "Error: Invalid sequence number\n");
        } else {
            currentSeqNum++;
            sending_packet.ackNum = receive_packet.seqNum;
            sending_packet.ackBit = receive_packet.synBit;
            if (send_packet(sockfd, sending_packet, receiver_addr) < 0) {
                perror("Error: Failed to send third packet during disconnect.");
                exit(EXIT_FAILURE);
            } else {
                connection_finished = 1;
            }
        }
    }
}

void disconnect_from_receiver(int sockfd, struct Packet sending_packet, struct Packet receive_packet, 
                struct sockaddr_in receiver_addr, int currentSeqNum) {

    int connection_finished = 0;

    while(!connection_finished) {
        sending_packet.seqNum = currentSeqNum;
        sending_packet.finBit = 1;

        if (send_packet(sockfd, sending_packet, receiver_addr) < 0) {
            perror("Error: Failed to send first packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        if (recvfrom(sockfd, &receive_packet, sizeof(receive_packet), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr)) < 0) {
            perror("Error: Failed to receive first packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        if (receive_packet.ackBit != sending_packet.finBit) {
            printf(stderr, "Error: Invalid sequence number\n");
        } else {
            currentSeqNum++;
            sending_packet.ackNum = receive_packet.seqNum;
            sending_packet.ackBit = receive_packet.finBit;
            if (send_packet(sockfd, sending_packet, receiver_addr) < 0) {
                perror("Error: Failed to send third packet during disconnect.");
                exit(EXIT_FAILURE);
            } else {
                connection_finished = 1;
            }
        }
    }
}

// unsigned int send_ack(int sockfd, struct sockaddr_in sender_addr, unsigned int seq_num, unsigned int ack_num) {
//     struct Packet ack_packet;
//     ack_packet.seq_num = seq_num;
//     ack_packet.ack_num = ack_num;
//     return sender_packet(sockfd, ack_packet, sender_addr);
// }

// Function to handle acknowledgments from the receiver
// int handle_acknowledgments(int sockfd, struct sockaddr_in receiver_addr, int* current_seq_num, int* current_ack_num) {
//     struct Packet ack_packet;
//     socklen_t addr_len = sizeof(struct sockaddr);

//     // while (recvfrom(sockfd, &ack_packet, sizeof(struct Packet), 0, (struct sockaddr *)&receiver_addr, &addr_len) > 0) {
//     //     if (ack_packet.ack_num >= *base_seq_num) {
//     //         *base_seq_num = ack_packet.ack_num + 1; // Slide window forward
//     //     }
//     // }

//     recvfrom(sockfd, &ack_packet, sizeof(struct Packet), 0, (struct sockaddr *)&receiver_addr, &addr_len);
//     if (ack_packet.ackBit != ack_packet.se) {
//         if (ack_packet.ack_num == current_ack_num){
//             cwnd = cwnd / 2; 
//         }
//     } else if () {
//         ack_packet.data_size 
//     }
// }

void rsend(char* hostname, 
            unsigned short int hostUDPport, 
            char* filename, 
            unsigned long long int bytesToTransfer) 
{
    int sockfd;
    struct sockaddr_in receiver_addr;
    char buffer[MAX_DATA_SIZE];
    struct Packet sender_packet;
    struct Packet receive_packet; 
    unsigned int currentSeqNum = SEQ_NUM;
    unsigned int currentAckNum;
    socklen_t addr_size;
    pthread_t sender_thread_id;
    pthread_t ack_thread_id;
    unsigned long long int* bytesTransferred = 0;
    unsigned int cwnd = 1;
    struct SendThreadArgs sendArgs;

    // Create UDP Socket 
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Initialize receiver address 
    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(hostUDPport);
    inet_pton(AF_INET, hostname, &receiver_addr.sin_addr);

    addr_size = sizeof(receiver_addr);
    connect_to_receiver(sockfd, sender_packet, receive_packet, receiver_addr);

    // Open the file
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to open file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    sendArgs.file = file;
    sendArgs.currentSeq = 1;
    sendArgs.sockfd = sockfd;
    sendArgs.receiver_addr = &receiver_addr;
    sendArgs.bytesTransferred = &bytesTransferred;
    sendArgs.bytesToTransfer = bytesToTransfer;

    if (pthread_mutex_init(&thread_lock, NULL) != 0) { 
        printf("\n mutex init has failed\n"); 
        exit(EXIT_FAILURE); 
    } 

    if (pthread_create(&sender_thread_id, NULL, send_packets_continuously, (void *)&sendArgs)) {
        perror("Error: failed to create transmission thread");
        exit(EXIT_FAILURE);
    }

    currentSeqNum = 1;

    int finBitSent = 0;
    while (!finBitSent) {
        struct Packet ackPacket;
        size_t bytes_read = recvfrom(sockfd, &ackPacket, sizeof(ackPacket), 0, &receiver_addr, sizeof(receiver_addr));

        if (bytes_read > 0 && ackPacket.ackBit && (ackPacket.ackNum >= currentSeqNum)) {
            currentSeqNum = ackPacket.ackNum + 1;
        } else {
            // Finish sending data because of packet loss and end thread and join it back
            pthread_mutex_lock(&thread_lock);
            continue_thread = 0;
            pthread_mutex_unlock(&thread_lock);
            if (pthread_join(sender_thread_id, NULL) != 0) {
                perror("pthread_join");
                exit(EXIT_FAILURE);
            }
            send_rate = send_rate / DECREASE_RATE;

            // sendArgs added to last send that has been acknowledged
            sendArgs.file = file;
            sendArgs.currentSeq = currentSeqNum;
            sendArgs.sockfd = sockfd;
            sendArgs.receiver_addr = &receiver_addr;
            sendArgs.bytesTransferred = &bytesTransferred;
            sendArgs.bytesToTransfer = bytesToTransfer;

            // start sending again 
            if (pthread_create(&sender_thread_id, NULL, send_packets_continuously, (void *)&sendArgs)) {
                perror("Error: failed to create transmission thread");
                exit(EXIT_FAILURE);
            }
        }
   }
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

    // printf("Arguments being used: \n");
    // for (int i = 0; i < argc; i++) {

    //     printf("%s ", argv[i]);
    //     printf("\n");
    // }

    rsend(hostname, hostUDPport, filename, bytesToTransfer);

    return (EXIT_SUCCESS);
}
