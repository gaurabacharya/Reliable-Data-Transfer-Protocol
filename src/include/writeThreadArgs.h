#ifndef WRITETHREADARGS_H
#define WRITETHREADARGS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>

struct WriteThreadArgs {
    FILE                    *file;
    struct PacketQueue      *packetQueue;
    unsigned long long int  writeRate;
};

#endif