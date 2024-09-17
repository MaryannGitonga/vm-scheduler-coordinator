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
	int ndomains, result;

	// 2. get all active running VMs
	ndomains = virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_RUNNING);

	if(ndomains < 0) {
		fprintf(stderr, "Failed to get active running VMs\n");
		return;
	}

	for (int i = 0; i < ndomains; i++)
	{
		domain = domains[i];
		result = virDomainSetMemoryStatsPeriod(domain, interval, 0);
		if (result != 0)
		{
			fprintf(stderr, "Failed to set memory stats period.\n");
		}

		// get memory stats
        virDomainMemoryStatStruct stats[VIR_DOMAIN_MEMORY_STAT_NR];
        int nr_stats = virDomainMemoryStats(domain, stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);
        if (nr_stats < 0) {
            fprintf(stderr, "Failed to get memory stats for domain ID %d.\n", virDomainGetID(domain));
            continue;
        }

		printf("Memory Statistics for Domain ID %d:\n", virDomainGetID(domain));
        for (int j = 0; j < nr_stats; j++) {
            switch (stats[j].tag) {
                case VIR_DOMAIN_MEMORY_STAT_SWAP_IN:
                    printf("  SWAP_IN: %llu KB\n", stats[j].val);
                    break;
                case VIR_DOMAIN_MEMORY_STAT_SWAP_OUT:
                    printf("  SWAP_OUT: %llu KB\n", stats[j].val);
                    break;
                case VIR_DOMAIN_MEMORY_STAT_MAJOR_FAULT:
                    printf("  MAJOR_FAULT: %llu\n", stats[j].val);
                    break;
                case VIR_DOMAIN_MEMORY_STAT_MINOR_FAULT:
                    printf("  MINOR_FAULT: %llu\n", stats[j].val);
                    break;
                case VIR_DOMAIN_MEMORY_STAT_UNUSED:
                    printf("  UNUSED: %llu KB\n", stats[j].val);
                    break;
                case VIR_DOMAIN_MEMORY_STAT_AVAILABLE:
                    printf("  AVAILABLE: %llu KB\n", stats[j].val);
                    break;
                case VIR_DOMAIN_MEMORY_STAT_USABLE:
                    printf("  USABLE: %llu KB\n", stats[j].val);
                    break;
                case VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON:
                    printf("  ACTUAL_BALLOON: %llu KB\n", stats[j].val);
                    break;
                case VIR_DOMAIN_MEMORY_STAT_LAST_UPDATE:
                    printf("  LAST_UPDATE: %llu\n", stats[j].val);
                    break;
                case VIR_DOMAIN_MEMORY_STAT_DISK_CACHES:
                    printf("  DISK_CACHES: %llu KB\n", stats[j].val);
                    break;
                case VIR_DOMAIN_MEMORY_STAT_HUGETLB_PGALLOC:
                    printf("  HUGETLB_PGALLOC: %llu\n", stats[j].val);
                    break;
                case VIR_DOMAIN_MEMORY_STAT_HUGETLB_PGFAIL:
                    printf("  HUGETLB_PGFAIL: %llu\n", stats[j].val);
                    break;
                default:
                    printf("  UNKNOWN STAT (%d): %llu\n", stats[j].tag, stats[j].val);
                    break;
            }
        }
		
	}

	free(domains);
	
}
