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
#define MAX_WINDOW_SIZE 10
#define TIMOUT_SEC 1 

struct Packet {
    unsigned int seq_num;
    unsigned int ack_num;
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

// Function to handle acknowledgments from the receiver
void handle_acknowledgments(int sockfd, struct sockaddr_in receiver_addr, int *base_seq_num) {
    struct Packet ack_packet;
    socklen_t addr_len = sizeof(struct sockaddr);

    while (recvfrom(sockfd, &ack_packet, sizeof(struct Packet), 0, (struct sockaddr *)&receiver_addr, &addr_len) > 0) {
        if (ack_packet.ack_num >= *base_seq_num) {
            *base_seq_num = ack_packet.ack_num + 1; // Slide window forward
        }
    }
}

void rsend(char* hostname, 
            unsigned short int hostUDPport, 
            char* filename, 
            unsigned long long int bytesToTransfer) 
{
    int sockfd;
    struct sockaddr_in receiver_addr;

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

    // Open the file
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to open file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    // Initialize sender window 
    // int base = 0;
    // int nextseqnum = 0;

    // Initialize sequence number and window size
    int base_seq_num = 0;
    int window_size = MAX_WINDOW_SIZE;

    // Create and send packets
    struct Packet packets[MAX_WINDOW_SIZE];

    while (!feof(file)) {
        // Send packets in the current window
        for (int i = 0; i < window_size; i++) {
            // Read data from file
            int bytes_read = fread(packets[i].data, bytesToTransfer, 1, file);
            if (bytes_read == 0) {
                // End of file reached
                break;
            }

            // Assign sequence number to packet
            packets[i].seq_num = base_seq_num + i;

            // Send packet
            send_packet(sockfd, *packets, receiver_addr);
        }

        // Handle acknowledgments
        handle_acknowledgments(sockfd, receiver_addr, &base_seq_num);
    }

    // Close file and socket
    fclose(file);
    close(sockfd);
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

    printf("Arguments being used: \n");
    for (int i = 0; i < argc; i++) {

        printf("%s ", argv[i]);
        printf("\n");
    }

    rsend(hostname, hostUDPport, filename, bytesToTransfer);

    return (EXIT_SUCCESS);
}