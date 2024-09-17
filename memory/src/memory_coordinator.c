#include<stdio.h>
#include<stdlib.h>
#include<libvirt/libvirt.h>
#include<math.h>
#include<string.h>
#include<unistd.h>
#include<limits.h>
#include<signal.h>
#define MIN(a,b) ((a)<(b)?a:b)
#define MAX(a,b) ((a)>(b)?a:b)

int is_exit = 0; // DO NOT MODIFY THE VARIABLE
typedef struct {
    double actual;
    double unused;
    double maxLimit;
} DomainMemoryStats;

DomainMemoryStats *domainStats = NULL;

void cleanUp() {
	free(domainStats);
}

void MemoryScheduler(virConnectPtr conn,int interval);

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

	cleanUp()
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
	virDomainPtr *domains, domain;
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
		domain = domains[i];

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

		domainStats[i].actual = actual;
        domainStats[i].unused = unused;
        domainStats[i].maxLimit = maxLimit;

		printf("Memory (VM %d) Actual: [%.2f MB], Unused: [%.2f MB], MaxLimit: [%.2f MB]\n", i, actual, unused, maxLimit);
		
	}

	for (int i = 0; i < ndomains; i++)
	{
		// check if unused memory is less than 100MB for the vm -> vm is consuming memory AND actual memory has not exceeded the limit
		while (domainStats[i].unused < 100 && domainStats[i].actual < domainStats[i].maxLimit)
		{
			int sacrificedVM = -1;
			for (int j = 0; j < ndomains; j++)
			{
				double releasedMemory = 0;
				// get memomory from other vms if vm has more than 200MB (actual) or more than 100MB (unused)
				if (j != i)
				{
					if (domainStats[j].unused > 100)
					{
						releasedMemory = MIN((domainStats[j].unused - 100), 100);
						domainStats[j].unused = domainStats[j].unused - releasedMemory;
						sacrificedVM = j;
						continue;
					} else if (domainStats[j].actual > 200)
					{
						releasedMemory = MIN((domainStats[j].actual - 200), 100);
						domainStats[j].actual = domainStats[j].actual - releasedMemory;
						if (virDomainSetMemory(domains[j], domainStats[j].actual * 1024) != 0)
						{
							fprintf(stderr, "Failed to set actual memory of %.2f MB to sacrificed domain %d\n", domainStats[j].actual, j);
						}
						
						printf("Domain %d sacrificed actual memory of %.2f MB\n", j, domainStats[j].actual);
						sacrificedVM = j;
						continue;
					}
				}

				if (releasedMemory != 0)
				{
					domainStats[i].actual = domainStats[i].actual + releasedMemory;
					if(virDomainSetMemory(domains[i], domainStats[i].actual * 1024)){
						fprintf(stderr, "Failed to set actual memory of %.2f MB to the starving domain %d\n", domainStats[i].actual, i);
					}
					printf("Starving domain %d now has memory of %.2f MB from a sacrificed domain\n", j, domainStats[j].actual);
				}
				
				
			}

			// if no vm was sacrificed, get memory from host
			if (sacrificedVM == -1)
			{
				if (hostFreeMemory > 200)
				{
					double releasedMemory = MIN((hostFreeMemory - 200), 100);
					hostFreeMemory = hostFreeMemory - releasedMemory;
					domainStats[i].actual = domainStats[i].actual + releasedMemory;
					if(virDomainSetMemory(domains[i], domainStats[i].actual * 1024)){
						fprintf(stderr, "Failed to set actual memory of %.2f MB to the starving domain %d\n", domainStats[i].actual, i);
					}
					printf("Starving domain %d now has memory of %.2f MB from the host\n", i, domainStats[i].actual);
				}
				
			}
		}

		// once vm has reached max limit, release memory until it's back to the initial memory 512MB (2048MB / 4 vms)
		while (domainStats[i].actual > (domainStats[i].maxLimit/4)){
			double releasedMemory = MIN((domainStats[i].actual - 100), 100);
			domainStats[i].actual = domainStats[i].actual - releasedMemory;
			if(virDomainSetMemory(domains[i], domainStats[i].actual * 1024)){
				fprintf(stderr, "Failed to set actual memory of %.2f MB to the bloated domain %d\n", domainStats[i].actual, i);
			}
			printf("Bloated domain %d now has memory of %.2f MB after releasing memory.\n", i, domainStats[i].actual);
		}
	}

	free(domains);
	
}
