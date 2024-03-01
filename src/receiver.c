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

#define MAX_DATA_SIZE 1024

struct Packet {
    unsigned int seq_num;
    unsigned int ack_num;
    unsigned int checksum;
    char data[MAX_DATA_SIZE];
};

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

unsigned int send_acknowledgment(int sockfd, struct sockaddr_in sender_addr, unsigned int ack_num) {
    struct Packet ack_packet;
    ack_packet.ack_num = ack_num;
    return send_packet(sockfd, ack_packet, sender_addr);
}

void rrecv(unsigned short int myUDPport, 
            char* destinationFile, 
            unsigned long long int writeRate) {
    int sockfd;
    struct sockaddr_in sender_addr;
    char buffer[1024];
    ssize_t bytesRead;

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Initialize receiver address
    memset(&sender_addr, 0, sizeof(sender_addr));
    sender_addr.sin_family = AF_INET;
    sender_addr.sin_port = htons(myUDPport);
    sender_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Open the file
    FILE* file = fopen(destinationFile, "w");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to open file %s\n", destinationFile);
        exit(EXIT_FAILURE);
    }

    // Receive packets and send acks
    while ((bytesRead = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&sender_addr, sizeof(sender_addr))) > 0) {
        fwrite(buffer, sizeof(char), bytesRead, file);
        send_acknowledgment(sockfd, sender_addr, bytesRead);
    }

    // Close the socket:
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
}
