#ifndef VCPU_SCHEDULER_H
#define VCPU_SCHEDULER_H

#include <libvirt/libvirt.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// struct to hold domain CPU statistics
typedef struct {
    double usage;
    double *prevTimes;
    unsigned char cpumap;
    int pinnedPcpu;
} DomainCPUStats;

double getStdDev(double *values, int nvalues, double *meanOut);
int areCpusBalanced(double *cpuUsages, int ncpus, double *meanOut, double *stdDevOut);
int DomainCPUStats_initializeForDomain(DomainCPUStats *domainStats, int npcpus);
void DomainCPUStats_deinitializeForDomain(DomainCPUStats *domainStats);
void DomainCPUStats_free(DomainCPUStats *stats, int ndomains);
DomainCPUStats* DomainCPUStats_create(int ndomains, int ncpus);
void CPUScheduler(virConnectPtr conn, int interval);
void signal_callback_handler(void);

extern DomainCPUStats *domainStats;
extern int ndomains;
extern int firstIteration;
extern int is_exit;

#endif
