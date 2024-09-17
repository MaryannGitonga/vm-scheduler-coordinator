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
	int ndomains;
	unsigned long long hostFreeMemory;

	// get all active running VMs
	ndomains = virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_RUNNING);
	if(ndomains < 0) {
		fprintf(stderr, "Failed to get active running VMs\n");
		return;
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

	unsigned long long hostFreeMemoryMB = hostFreeMemory / (1024 * 1024);
	printf("Host free memory: %llu MB\n", hostFreeMemoryMB);

	for (int i = 0; i < ndomains; i++)
	{
		domain = domains[i];

		// get memory parameters
        virTypedParameterPtr params = NULL;
        int nparams = 0;
        if (virDomainGetMemoryParameters(domain, NULL, &nparams, 0) == 0 && nparams > 0) {
            params = malloc(sizeof(*params) * nparams);
            if (params == NULL) {
                fprintf(stderr, "Memory allocation failed\n");
                free(domains);
                return;
            }
            memset(params, 0, sizeof(*params) * nparams);
            if (virDomainGetMemoryParameters(domain, params, &nparams, 0) < 0) {
                fprintf(stderr, "Failed to get memory parameters for domain %d\n", i);
                free(params);
                continue;
            }
        }

        unsigned long long hardLimit = 0;
        for (int j = 0; j < nparams; j++) {
            if (strcmp(params[j].field, "hard_limit") == 0){
                hardLimit = params[j].value.ul / 1024;
			}
        }
        free(params);

		// get memory stats
        virDomainMemoryStatStruct stats[VIR_DOMAIN_MEMORY_STAT_NR];
        int nr_stats = virDomainMemoryStats(domain, stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);

        if (nr_stats < 0) {
            fprintf(stderr, "Failed to get memory stats for domain %d\n", i);
            continue;
        }

		unsigned long long actual = 0, unused = 0;
        for (int j = 0; j < nr_stats; j++) {
            if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON)
                actual = stats[j].val;
            if (stats[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED)
                unused = stats[j].val;
        }

		// convert to MB
        unsigned long long actualMB = actual / 1024;
        unsigned long long unusedMB = unused / 1024;

		printf("Memory (VM %d) Actual: [%llu MB], Unused: [%llu MB], HardLimit: [%llu MB]\n", i, actualMB, unusedMB, hardLimit);
	}

	free(domains);
	
}
