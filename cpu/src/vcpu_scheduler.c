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
	virDomainPtr *domains, domain;
	int ndomains, result, nparams, npcpuStats, npcpus;
	virTypedParameterPtr params;
	virNodeCPUStatsPtr pcpuStats;

	// 2. get all active running VMs
	ndomains = virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_RUNNING);

	if(ndomains < 0){
		fprintf(stderr, "Failed to get active running VMs\n");
		return;
	}

	// 3. get pcpu stats
	npcpus = virNodeGetCPUMap(conn, NULL, NULL, 0);
    if (npcpus < 0) {
        fprintf(stderr, "Failed to get number of pcpus\n");
        free(domains);
        return;
    }

	pcpuStats = calloc(npcpus, sizeof(virNodeCPUStats));
	npcpuStats = VIR_NODE_CPU_STATS_ALL_CPUS;

	for (int i = 0; i < npcpus; i++) {
		result = virNodeGetCPUStats(conn, i, pcpuStats, &npcpuStats, 0);
		if (result < 0) {
			fprintf(stderr, "Failed to get pCPU stats for pCPU %d\n", i);
			free(pcpuStats);
			free(domains);
			return;
		}
	}

	long long *pcpuLoads = calloc(npcpus, sizeof(long long));

	for (int i = 0; i < ndomains; i++)
	{
		// 3. collect vcpu stats
		// chose virDomainGetCPUStats over virDomainGetInfo because of the level of accuracy in cpu time value
		domain = domains[i];

		nparams = virDomainGetCPUStats(domain, NULL, 0, -1, 1, 0);
		if (nparams < 0)
		{
			fprintf(stderr, "Failed to the num of parameter for domain %d\n", i);
			continue;
		}

		params = calloc(nparams, sizeof(virTypedParameter));

		result = virDomainGetCPUStats(domain, params, nparams, -1, 1, 0);

		if (result < 0)
		{
			fprintf(stderr, "Failed to get vcpu stats for domain %d\n", i);
			free(params);
			continue;
		}

		// 5. determine map affinity -> 1 vcpu per domain
		unsigned char *cpuMaps = calloc(1, VIR_CPU_MAPLEN(npcpus));
		result = virDomainGetVcpuPinInfo(domain, 1, cpuMaps, VIR_CPU_MAPLEN(npcpus), 0);
		if (result < 0) {
			fprintf(stderr, "Failed to get vcpu pinning info for domain %d\n", i);
			free(params);
			free(cpuMaps);
			continue;
		}

		for (int j = 0; j < nparams; j++) {
			if (strcmp(params[j].field, "cpu_time") == 0)
			{
				unsigned long long vcpuTime = params[j].value.ul;
				for (int k = 0; k < npcpus; k++) {
					if (VIR_CPU_USED(cpuMaps, k)) {
                        pcpuLoads[k] += vcpuTime;
                    }
				}
				printf("  CPU time for domain %d: %llu\n", i, vcpuTime);
			}
        }

		// 6. find "best" pcpu to pin vcpu
		int bestPCPU = 0;
		long long minLoad = pcpuLoads[0];
		for (int k = 1; k < npcpus; k++) {
			if (pcpuLoads[k] < minLoad) {
				bestPCPU = k;
				minLoad = pcpuLoads[k];
			}
		}

		// 7. change pcpu assigned to vcpu
		unsigned char *bestPCPUMap = calloc(VIR_CPU_MAPLEN(npcpus), sizeof(unsigned char));
		memset(bestPCPUMap, 0, VIR_CPU_MAPLEN(npcpus));
        bestPCPUMap[bestPCPU / 8] |= (1 << (bestPCPU % 8));

		result = virDomainPinVcpu(domain, 0, bestPCPUMap, VIR_CPU_MAPLEN(npcpus));
		if (result < 0) {
			fprintf(stderr, "Failed to pin vcpu to pcpu %d for domain %d\n", bestPCPU, i);
		}

		free(bestPCPUMap);
		free(params);
		free(cpuMaps);
	}

	free(pcpuLoads);
    free(pcpuStats);
	free(domains);
}




