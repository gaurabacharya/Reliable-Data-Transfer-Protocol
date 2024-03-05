#ifndef PACKETQUEUE_H
#define PACKETQUEUE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>

#include "./packet.h"

struct PacketQueue {
    struct Packet   *packets;
    int             front;
    int             rear;
    int             size;
    int             capacity;
    int             packetLoss;
};

void initPacketQueue(struct PacketQueue *queue, int capacity);
int isPacketQueueEmpty(struct PacketQueue *queue);
int isPacketQueueFull(struct PacketQueue *queue);
int getpacketQueueEmptySpace(struct PacketQueue *queue);
void enqueuePacket(struct PacketQueue *queue, struct Packet packet);
void enqueuePacketFromFront(struct PacketQueue *queue, struct Packet packet);
struct Packet dequeuePacket(struct PacketQueue *queue);
struct Packet frontPacket(struct PacketQueue *queue);
void freePacketQueue(struct PacketQueue *queue);

#endif