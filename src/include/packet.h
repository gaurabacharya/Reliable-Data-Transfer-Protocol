#ifndef PACKET_H
#define PACKET_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>

struct Packet {
    uint16_t    sourcePort;
    uint16_t    destinationPort;
    uint32_t    seqNum;
    uint32_t    ackNum;
    uint16_t    ackBit;
    uint16_t    synBit;
    uint16_t    finBit;
    uint16_t    windowSize;
    ssize_t     dataSize;
    char        data[1460];
};

#endif