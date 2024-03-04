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

void connect_to_sender(int sockfd, struct sockaddr *sender_addr, socklen_t addr_len, int *seqNum) {
    struct Packet *recv_packet;
    struct Packet *send_packet;
    int connection_established = 0;

    recv_packet = malloc(sizeof(struct Packet));
    send_packet = malloc(sizeof(struct Packet));

    // Begin three-way handshake
    while (!connection_established) {
        if (recvfrom(sockfd, recv_packet, sizeof(struct Packet), 0, sender_addr, &addr_len) < 0) {
            perror("Error: Failed to begin three-way handshake.");
            exit(EXIT_FAILURE);
        }

        if (recv_packet->synBit == 1) {
            send_packet->synBit = 1;
            send_packet->ackBit = 1;
            send_packet->seqNum = *seqNum;
            send_packet->ackNum = recv_packet->seqNum;
            
            if (sendto(sockfd, send_packet, sizeof(struct Packet), 0, sender_addr, addr_len) < 0) {
                perror("Error: Failed to send second packet in three-way handshake.");
                exit(EXIT_FAILURE);
            }

            if (recvfrom(sockfd, recv_packet, sizeof(struct Packet), 0, sender_addr, &addr_len) < 0) {
                perror("Error: Failed to receive third packet in three-way handshake.");
                exit(EXIT_FAILURE);
            }

            if (recv_packet->ackBit == 1 && recv_packet->ackNum == send_packet->seqNum) {
                printf("Connection established.\n");
                connection_established = 1;
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

void disconnect_from_sender(int sockfd, struct sockaddr *sender_addr, socklen_t addr_len, int *seqNum) {
    struct Packet *recv_packet;
    struct Packet *send_packet;
    int connection_finished = 0;

    recv_packet = malloc(sizeof(struct Packet));
    send_packet = malloc(sizeof(struct Packet));

    // Begin disconnection
    while (!connection_finished) {
        if (recvfrom(sockfd, recv_packet, sizeof(struct Packet), 0, sender_addr, &addr_len) < 0) {
            perror("Error: Failed to receive first packet during disconnect.");
            exit(EXIT_FAILURE);
        }

        if (recv_packet->finBit == 1) {
            send_packet->seqNum = *seqNum;
            send_packet->ackBit = 1;
            send_packet->ackNum = recv_packet->seqNum;
            
            if (sendto(sockfd, send_packet, sizeof(struct Packet), 0, sender_addr, addr_len) < 0) {
                perror("Error: Failed to send second packet during disconnect.");
                exit(EXIT_FAILURE);
            }

            *seqNum = *seqNum + 1;

            send_packet->seqNum = *seqNum;
            send_packet->ackBit = 0;
            send_packet->ackNum = recv_packet->seqNum;
            send_packet->finBit = 1;

            if (sendto(sockfd, send_packet, sizeof(struct Packet), 0, sender_addr, addr_len) < 0) {
                perror("Error: Failed to send third packet during disconnect.");
                exit(EXIT_FAILURE);
            }

            *seqNum = *seqNum + 1;

            if (recvfrom(sockfd, recv_packet, sizeof(struct Packet), 0, sender_addr, &addr_len) < 0) {
                perror("Error: Failed to receive fourth packet during disconnect.");
                exit(EXIT_FAILURE);
            }
            
            if (recv_packet->ackBit == 1 && recv_packet->ackNum == send_packet->seqNum) {
                printf("Connection terminated.\n");
                connection_finished = 1;
            }
            else {
                perror("Error: Failed to disconnect at the last stage");
            }
        }
        else {
            perror("Error: Failed to disconnect at the first stage.");
        }
    }
}

void receive_data(int sockfd, struct sockaddr *sender_addr, socklen_t addr_len, int *seqNum, char* destinationFile, unsigned long long int writeRate) {
    struct Packet *recv_packet;
    struct Packet *send_packet;

    recv_packet = malloc(sizeof(struct Packet));
    send_packet = malloc(sizeof(struct Packet));
    
    // Open the file
    FILE* file = fopen(destinationFile, "w+");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to open file %s\n", destinationFile);
        exit(EXIT_FAILURE);
    }


}

void rrecv(unsigned short int myUDPport,
           char* destinationFile, 
           unsigned long long int writeRate) {
    int sockfd;
    struct sockaddr_in receiver_addr, sender_addr;
    socklen_t addr_len = sizeof(sender_addr);
    int *seqNum = 1000;

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Set up the receiver address
    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(myUDPport);
    receiver_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the socket
    if (bind(sockfd, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr)) == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    // Connect to sender
    connect_to_sender(sockfd, (struct sockaddr *)&sender_addr, addr_len, seqNum);

    // Receive data

    // Disconnect from sender
    disconnect_from_sender(sockfd, (struct sockaddr *)&sender_addr, addr_len, seqNum);
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
