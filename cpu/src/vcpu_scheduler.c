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

int is_exit = 0; // DO NOT MODIFY THIS VARIABLE


void CPUScheduler(virConnectPtr conn,int interval);

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

	// Get the total number of pCpus in the host
	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	// Run the CpuScheduler function that checks the CPU Usage and sets the pin at an interval of "interval" seconds
	{
		CPUScheduler(conn, interval);
		sleep(interval);
	}

	// Closing the connection
	virConnectClose(conn);
	return 0;
}

/* COMPLETE THE IMPLEMENTATION */
void CPUScheduler(virConnectPtr conn, int interval)
{
	virDomainPtr *domains;
	int ndomains, npcpuParams, result, nvcpus, npcpus;
	virNodeCPUStatsPtr pcpuParams;
	unsigned char *cpuMaps;

	// collect host pcpu stats
	npcpus = virNodeGetCPUMap(conn, NULL, NULL, 0);
	if (npcpus < 0) {
        fprintf(stderr, "Failed to get npcpus\n");
        return;
    }

	if (virNodeGetCPUStats(conn, -1, NULL, &npcpuParams, 0) < 0)
	{
		fprintf(stderr, "Failed to get nparams for pcpu stats\n");
        return;
	}

	pcpuParams = calloc(npcpuParams, sizeof(virNodeCPUStats));
	result = virNodeGetCPUStats(conn, -1, pcpuParams, &npcpuParams, 0);
	if (result < 0) {
        fprintf(stderr, "Failed to get pcpu stats\n");
        free(pcpuParams);
        return;
    }

	// get all active running VMs
	ndomains = virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_RUNNING);

	if(ndomains < 0){
		fprintf(stderr, "Failed to get active running VMs\n");
		free(pcpuParams);
		return;
	}
	
	// collect vcpu stats
	for (int i = 0; i < ndomains; i++)
	{
		// get the number of vcpus in the domain
        nvcpus = virDomainGetMaxVcpus(domains[i]);
        if (nvcpus < 0) {
            fprintf(stderr, "Failed to get nvcpus for domain %d\n", i);
            continue;
        }

        // cpuMaps = malloc(nvcpus * VIR_CPU_MAPLEN(npcpus));

		// Single vcpu or multiple? (above is multiple)
		cpuMaps = malloc(VIR_CPU_MAPLEN(npcpus));
        if (cpuMaps == NULL) {
            fprintf(stderr, "Failed to allocate memory for CPU maps\n");
            continue;
        }
        memset(cpuMaps, 0, VIR_CPU_MAPLEN(npcpus));

		// determine best pcpu
		for (int j = 0; j < nvcpus; j++)
		{
			int bestpcpu = -1;
			long long minLoad = LLONG_MAX;

			for (int k = 0; k < npcpuParams; k++)
			{
				long long totalLoad = pcpuParams[k].user + pcpuParams[k].kernel;
				if (totalLoad < minLoad)
				{
					minLoad = totalLoad;
					bestpcpu = k;
				}
			}

			if (bestpcpu != -1)
			{
				cpuMaps[bestpcpu / 8] |= (1 << (bestpcpu % 8));

				result = virDomainPinVcpu(domains[i], j, cpuMaps, VIR_CPU_MAPLEN(npcpus));
				if (result < 0)
				{
					fprintf(stderr, "Failed to pin vcpu %d to pcpu %d for domain %d\n", j, bestpcpu, i);
				}
			}
		}

		free(cpuMaps);
	}

	free(pcpuParams);
	free(domains);
}




