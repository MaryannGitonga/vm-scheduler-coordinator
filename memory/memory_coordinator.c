#include <stdio.h>
#include <stdlib.h>
#include <libvirt/libvirt.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <memory_coordinator.h>

#define MIN(a,b) ((a)<(b)?a:b)
#define MAX(a,b) ((a)>(b)?a:b)

int is_exit = 0; // DO NOT MODIFY THE VARIABLE
DomainMemoryStats *domainStats = NULL;
int *starvingVMs = NULL;
int nStarvingVMs = 0;
double lowerBoundMemory = 200.0;
double unusedThreshold = 100.0;

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
	printf("Caught Signal");
	is_exit = 1;
}

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
int main(int argc, char *argv[])
{
	virConnectPtr conn;

	if(argc != 2)
	{
		printf("Incorrect number of arguments\n");
		return 0;
	}

	// Gets the interval passes as a command line argument and sets it as the STATS_PERIOD for collection of balloon memory statistics of the domains
	int interval = atoi(argv[1]);
	
	conn = virConnectOpen("qemu:///system");
	if(conn == NULL)
	{
		fprintf(stderr, "Failed to open connection\n");
		return 1;
	}

	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	{
		// Calls the MemoryScheduler function after every 'interval' seconds
		MemoryScheduler(conn, interval);
		sleep(interval);
	}

	cleanUp();
	// Close the connection
	virConnectClose(conn);
	return 0;
}

/*
COMPLETE THE IMPLEMENTATION
*/
void MemoryScheduler(virConnectPtr conn, int interval)
{
	printf("Scheduler started...\n");
	virDomainPtr *domains = NULL;
	int ndomains;
	unsigned long long hostFreeMemory;

	// get all active running VMs
	ndomains = virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_RUNNING);
	if(ndomains < 0) {
		fprintf(stderr, "Failed to get active running VMs\n");
		return;
	}

	if (domainStats == NULL) {
        domainStats = malloc(ndomains * sizeof(DomainMemoryStats));
		memset(domainStats, 0, ndomains * sizeof(DomainMemoryStats)); 
        if (domainStats == NULL) {
            fprintf(stderr, "Failed to allocate memory for domain stats\n");
            free(domains);
            return;
        }
    }

	// set memory stats period for all domains
    for (int i = 0; i < ndomains; i++) {
        if (virDomainSetMemoryStatsPeriod(domains[i], interval, 0) < 0) {
            fprintf(stderr, "Failed to set memory stats period for VM %d\n", i);
            continue;
        }
    }

	// get host memory info
	hostFreeMemory = virNodeGetFreeMemory(conn);
	if (hostFreeMemory == 0) {
        fprintf(stderr, "Failed to get host free memory\n");
        free(domains);
        return;
    }

	double hostFreeMemoryMB = hostFreeMemory / (1024 * 1024);
	printf("Host free memory: %.2f MB\n", hostFreeMemoryMB);

	for (int i = 0; i < ndomains; i++)
	{
		virDomainPtr domain = domains[i];

        double maxLimit = 0;
        maxLimit = virDomainGetMaxMemory(domain) / 1024;

		// get memory stats
        virDomainMemoryStatStruct stats[VIR_DOMAIN_MEMORY_STAT_NR];
        int nr_stats = virDomainMemoryStats(domain, stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);

        if (nr_stats < 0) {
            fprintf(stderr, "Failed to get memory stats for domain %d\n", i);
            continue;
        }

		double actual = 0, unused = 0;
        for (int j = 0; j < nr_stats; j++) {
            if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON)
                actual = stats[j].val / 1024;
            if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED)
                unused = stats[j].val / 1024;
        }

		domainStats[i].prevUnused = domainStats[i].unused;
		domainStats[i].prevActual = domainStats[i].actual;
		domainStats[i].actual = actual;
        domainStats[i].unused = unused;
        domainStats[i].maxLimit = maxLimit;

		printf("Memory (VM %d) Actual: [%.2f MB], PrevActual: [%.2f MB], Unused: [%.2f MB], PrevUnused: [%.2f MB] MaxLimit: [%.2f MB]\n", i, domainStats[i].actual, domainStats[i].prevActual, domainStats[i].unused, domainStats[i].prevUnused, domainStats[i].maxLimit);
		
	}
	

	// get starving domains -> marked as starving from prev iter.
	if (nStarvingVMs == 0)
	{	// if not initilized yet
		if (starvingVMs == NULL)
		{
			starvingVMs = malloc(ndomains * sizeof(int));
		}
		
		memset(starvingVMs, 0, ndomains * sizeof(int));
		for (int i = 0; i < ndomains; i++)
		{
			// if vm has unused that's decreasing and is less than or equal to 100MB (about to be exhausted)
			int unusedReducing = domainStats[i].prevUnused > 0.0 && domainStats[i].unused < unusedThreshold && (domainStats[i].prevUnused - domainStats[i].unused);
			int isStarving = (unusedReducing && !domainStats[i].readyToRelease && (domainStats[i].actual >= domainStats[i].maxLimit/4));
			if (isStarving){
				printf("Domain %d is starving...\n", i);
				starvingVMs[i] = 1;
				nStarvingVMs += 1;
			}
		}
	}
	

	printf("Number of starving VMS....%d\n", nStarvingVMs);

	for (int i = 0; i < ndomains; i++)
	{

		for (int s = 0; s < nStarvingVMs; s++)
		{
			double unusedDiff = domainStats[i].prevUnused - domainStats[i].unused > 0 ? domainStats[i].prevUnused - domainStats[i].unused : 0;
			// only allocate starving vm memory it requires to get to threshold
			domainStats[s].memoryToAllocate = MIN(50.0, unusedThreshold - domainStats[s].unused);
			domainStats[s].memoryToAllocate = MAX(unusedDiff, domainStats[s].memoryToAllocate) * 2;
			printf("Memory allocatable for starving domain %d...%2f\n", i, domainStats[s].memoryToAllocate);
		}

		// if porgram is terminated, the unused memory increases
		double unusedDiff = domainStats[i].unused - domainStats[i].prevUnused;
		int programTerminated = domainStats[i].prevUnused > 0.0 && unusedDiff > 200;
		if (starvingVMs[i] && programTerminated)
		{
			domainStats[i].readyToRelease = 1;
			printf("Program in domain %d terminated: %d\n", i, domainStats[i].readyToRelease);
			printf("Unused Diff: %2f\n", unusedDiff);
		}
		
		// int unusedMemoryReduced = (domainStats[i].prevUnused > 0.0 && (domainStats[i].prevUnused - domainStats[i].unused > 10.0));
		// while the vm's unused memory is reducing AND the next possible actual memory has not exceeded the limit AND not attained max yet
		if (starvingVMs[i] && (domainStats[i].actual < domainStats[i].maxLimit) && !domainStats[i].readyToRelease)
		{
			printf("Re-allocating memory.....\n");
			int sacrificedVM = -1;
			double releasedMemory = 0;
			

			// if not all vms are starving, sacrifice idle vms
			if (nStarvingVMs != ndomains)
			{
				for (int j = 0; j < ndomains; j++)
				{
					// get memory from other vms if vm has more than 100MB (unused) and more than 200MB (actual)
					// ensure that starving vms are not sacrificed
					if (!starvingVMs[j])
					{ 
						if (domainStats[j].unused >= 100 && domainStats[j].actual > lowerBoundMemory)
						{
							releasedMemory = MIN((domainStats[j].actual - lowerBoundMemory), domainStats[i].memoryToAllocate);
							releasedMemory = MIN(releasedMemory, domainStats[i].maxLimit - domainStats[i].actual);

							domainStats[j].actual = domainStats[j].actual - releasedMemory;
							if (virDomainSetMemory(domains[j], domainStats[j].actual * 1024) != 0)
							{
								fprintf(stderr, "Failed to set actual memory of %.2f MB to sacrificed domain %d\n", domainStats[j].actual, j);
							}
							
							printf("Domain %d sacrificed actual memory of %.2f MB\n", j, releasedMemory);
							sacrificedVM = j;
							break;
						}
					}
				}
			}

			if (sacrificedVM != -1)
			{
				domainStats[i].actual = domainStats[i].actual + releasedMemory;
				if(virDomainSetMemory(domains[i], domainStats[i].actual * 1024) != 0){
					fprintf(stderr, "Failed to set actual memory of %.2f MB to the starving domain %d\n", domainStats[i].actual, i);
				}

				domainStats[i].readyToRelease = (domainStats[i].actual == domainStats[i].maxLimit);
				printf("Starving domain %d now has memory of %.2f MB from a sacrificed domain... attained max: %d\n", i, domainStats[i].actual, domainStats[i].readyToRelease);
			} else {
				// if no vm was sacrificed, get memory from host if host has more than 200MB (unused)
				if ((hostFreeMemory) >= lowerBoundMemory)
				{
					releasedMemory = MIN((hostFreeMemory - lowerBoundMemory), domainStats[i].memoryToAllocate);
					releasedMemory = MIN(releasedMemory, domainStats[i].maxLimit - domainStats[i].actual);

					hostFreeMemory = hostFreeMemory - releasedMemory;
					domainStats[i].actual = domainStats[i].actual + releasedMemory;
					if(virDomainSetMemory(domains[i], domainStats[i].actual * 1024) != 0){
						fprintf(stderr, "Failed to set actual memory of %.2f MB to the starving domain %d\n", domainStats[i].actual, i);
					}

					domainStats[i].readyToRelease = (domainStats[i].actual == domainStats[i].maxLimit);
					printf("Starving domain %d now has memory of %.2f MB from the host...\n", i, domainStats[i].actual);
				} else {
					// if host can no longer give memory, reclaim memory from starving vms
					domainStats[i].readyToRelease = 1;
					printf("Host has no more memory to give...\n");
				}
			}

			if (nStarvingVMs > 1)
			{
				continue;
			}

			break;
		}

		if (nStarvingVMs > 0)
		{
			// once starving vm has attained the max limit, release memory until it's back to the initial memory 512MB (2048MB / 4 vms)
			double lowestVMMemory = domainStats[i].maxLimit / 4;
			if (starvingVMs[i] && domainStats[i].readyToRelease)
			{
				if (domainStats[i].actual > lowestVMMemory){
					double releasedMemory = (domainStats[i].actual - 50) > lowestVMMemory ? MIN((domainStats[i].actual - 50), 50): (domainStats[i].actual - lowestVMMemory);
					domainStats[i].actual = domainStats[i].actual - releasedMemory;

					if(virDomainSetMemory(domains[i], domainStats[i].actual * 1024) != 0){
						fprintf(stderr, "Failed to set actual memory of %.2f MB to the bloated domain %d\n", domainStats[i].actual, i);
					}

					// change readyToRelease to 0 if actual memory is 512MB
					domainStats[i].readyToRelease = (domainStats[i].actual != lowestVMMemory);

					printf("Bloated domain %d now has memory of %.2f MB after releasing memory, ready to release %d\n", i, domainStats[i].actual, domainStats[i].readyToRelease);
					
					if (!domainStats[i].readyToRelease)
					{
						if (nStarvingVMs > 1)
						{
							starvingVMs[i] = 0; // vm is no longer starving
							nStarvingVMs -= 1;
							continue;
						}

						starvingVMs[i] = 0; // vm is no longer starving
						nStarvingVMs -= 1;
						break;
					}
				}
			}
		}
		
	}

	free(domains);
	
}

void cleanUp() {
	free(domainStats);
	free(starvingVMs);
}