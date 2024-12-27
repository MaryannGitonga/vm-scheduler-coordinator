#ifndef MEMORY_COORDINATOR_H
#define MEMORY_COORDINATOR_H

#include <libvirt/libvirt.h>

// sruct to store memory stats for each domain
typedef struct {
    double actual;
    double prevActual;
    double unused;
    double prevUnused;
    double maxLimit;
    int readyToRelease;
    double memoryToAllocate;
    int prevStarving;
} DomainMemoryStats;

extern DomainMemoryStats *domainStats;    // arr of domain memory stats
extern int *starvingVMs;                  // arr to track starving VMs
extern int nStarvingVMs;                  // no of starving VMs
extern double lowerBoundMemory;           // lower bound for memory threshold
extern double unusedThreshold;            // threshold for unused memory

// Function declarations
void cleanUp();
void MemoryScheduler(virConnectPtr conn, int interval);
void signal_callback_handler();

#endif