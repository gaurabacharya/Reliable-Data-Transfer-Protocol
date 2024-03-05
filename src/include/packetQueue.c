#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "packet.h"
#include "packetQueue.h"

// Function to initialize a packet queue
void initPacketQueue(struct PacketQueue *queue, int capacity) {
    queue->packets = (struct Packet *)malloc(capacity * sizeof(struct Packet));
    queue->front = 0;
    queue->rear = -1;
    queue->size = 0;
    queue->capacity = capacity;
    queue->packetLoss = 0;
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
    struct Packet packet = queue->packets[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
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