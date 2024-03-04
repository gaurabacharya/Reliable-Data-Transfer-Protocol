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
#define SEQ_NUM 1

pthread_mutex_t packet_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t packet_sent_cond = PTHREAD_COND_INITIALIZER;
int cwnd = 1;

void* check_acks(void* arg) {

}

struct Packet {
    unsigned int seq_num;
    unsigned int ack_num;
    char data[MAX_DATA_SIZE];
};

struct CheckAckArgs {
    int *sockfd;
    struct sockaddr_in *receiver_addr;
    unsigned int *seq_num;
    unsigned int *ack_num;
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



// unsigned int send_ack(int sockfd, struct sockaddr_in sender_addr, unsigned int seq_num, unsigned int ack_num) {
//     struct Packet ack_packet;
//     ack_packet.seq_num = seq_num;
//     ack_packet.ack_num = ack_num;
//     return sender_packet(sockfd, ack_packet, sender_addr);
// }

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
    char buffer[MAX_DATA_SIZE];
    struct Packet sender_packet;
    struct Packet receive_packet; 
    unsigned int currentSeqNum = SEQ_NUM;
    unsigned int currentAckNum;
    socklen_t addr_size;
    pthread_t sender_thread_id;
    pthread_t ack_thread_id;
    unsigned long long int bytesTransferred = 0;
    unsigned int cwnd = 1;

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

    // Send first message to establish connection
    sender_packet.seq_num = currentSeqNum;
    send_packet(sockfd, sender_packet, receiver_addr);
    // sendto(sockfd, &sender_packet, sizeof(sender_packet), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));

    // Receive write_rate from receiver
    ssize_t numBytes = recvfrom(sockfd, &receive_packet, sizeof(receive_packet), 0, (struct sockaddr *)&receiver_addr, &addr_size);
    if (receive_packet.ack_num != currentSeqNum) {
        fprintf(stderr, "Error: Invalid sequence number\n");
        exit(EXIT_FAILURE);
    }

    currentSeqNum++;
    currentAckNum = receive_packet.seq_num;
    
    // Send ack to receiver
    sender_packet.seq_num = currentSeqNum;
    sender_packet.ack_num = currentAckNum;
    send_packet(sockfd, sender_packet, receiver_addr);
    // sendto(sockfd, &sender_packet, sizeof(sender_packet), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
    currentSeqNum++;

    // Open the file
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        fprintf(stderr, "Error: Unable to open file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    // Take the bytesToTransfer, divide it by writeRate, and then get the remainder, and then send each one 
    // get the divider, take i from 0 to divider and sending those packets, then send the remainder packet, then finish
    // Your implementation should support flow control via a sliding window mechanism to ensure that the receiver is not overwhelmed. 
    // Up to this point, one could have assumed that writeRate at the receiver is not limited (i.e., writeRate == 0). 
    // When writeRate is limited, the receiver cannot maintain an infinite buffer and should signal to the sender to reduce the data transmission rate to be consistent with writeRate.
    // When flow control is needed then some of the bandwidth utilization expectations will change to be consistent with flow control.

    //setup struct for variables that need to be passed to threads 
    struct CheckAckArgs ack_args;
    ack_args.sockfd = &sockfd;
    ack_args.receiver_addr = &receiver_addr;
    ack_args.seq_num = &currentSeqNum;
    ack_args.ack_num = &currentAckNum;

    // create thread for sender 
    pthread_create(&ack_thread_id, NULL, &check_acks, (void *)&ack_args);

    // Step 1: Send 10 packets into network without waiting for any ACKs
    // Step 2: Receive ACK for the first packet sent and increase cwnd by 1
    
    while (!feof(file) || bytesTransferred <= bytesToTransfer) {
        // Send packets in the current window
        for (int i = 0; i < cwnd; i++) {
            struct Packet currPacket;
            // Read data from file
            int bytes_read = fread(currPacket.data, bytesToTransfer, 1, file);
            bytesTransferred += bytes_read;
            currPacket.seq_num = currentSeqNum;
            if (bytes_read == 0) {
                // End of file reached
                break;
            }
        }
    }
    //         // Assign sequence number to packet
    //         packets[i].seq_num = base_seq_num + i;

    //         // Send packet
    //         sender_packet(sockfd, *packets, receiver_addr);
    //     }

    //     // Handle acknowledgments
    //     handle_acknowledgments(sockfd, receiver_addr, &base_seq_num);
    // }

    // // Transfer data from file to the receiver
    // while (!feof(file)) {
    //     bytesRead = fread(buffer, 1, sizeof(buffer), file);
    //     if (bytesRead <= 0) {
    //         break;
    //     }

    //     if (sendto(sockfd, buffer, bytesRead, 0, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
    //         perror("sendto");
    //         exit(EXIT_FAILURE);
    //     }

    //     printf("Sent %ld bytes\n", bytesRead);

    //     bytesToTransfer -= bytesRead;
    // }

    // // Initialize sender window 
    // // int base = 0;
    // // int nextseqnum = 0;

    // // Initialize sequence number and window size
    // int base_seq_num = 0;
    // int window_size = MAX_WINDOW_SIZE;

    // // Create and send packets
    // struct Packet packets[MAX_WINDOW_SIZE];
    // int totalBytesTransferred = 0;

    // while (!feof(file) || totalBytesTransferred <= bytesToTransfer) {
    //     // Send packets in the current window
    //     for (int i = 0; i < window_size; i++) {
    //         // Read data from file
    //         int bytes_read = fread(packets[i].data, bytesToTransfer, 1, file);
    //         totalBytesTransferred += bytes_read;
    //         if (bytes_read == 0) {
    //             // End of file reached
    //             break;
    //         }
    //     }
    // }
    // //         // Assign sequence number to packet
    // //         packets[i].seq_num = base_seq_num + i;

    // //         // Send packet
    // //         sender_packet(sockfd, *packets, receiver_addr);
    // //     }

    // //     // Handle acknowledgments
    // //     handle_acknowledgments(sockfd, receiver_addr, &base_seq_num);
    // // }

    // // Close file and socket
    // fclose(file);
    // close(sockfd);
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
