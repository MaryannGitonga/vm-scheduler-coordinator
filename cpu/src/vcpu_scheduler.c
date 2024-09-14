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
	printf("Scheduler started...\n");
	virDomainPtr *domains, domain;
	int ndomains, result, nparams, npcpus;
	virTypedParameterPtr params;
	// static long long *prevPcpuLoads = NULL;

	// 2. get all active running VMs
	ndomains = virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_RUNNING);

	if(ndomains < 0) {
		fprintf(stderr, "Failed to get active running VMs\n");
		return;
	}

	npcpus = virNodeGetCPUMap(conn, NULL, NULL, 0);
    if (npcpus < 0) {
        fprintf(stderr, "Failed to get number of pcpus\n");
        free(domains);
        return;
    }

	// if (prevPcpuLoads == NULL) {
    //     prevPcpuLoads = calloc(npcpus, sizeof(long long));
    //     if (prevPcpuLoads == NULL) {
    //         fprintf(stderr, "Failed to allocate memory for prevPcpuLoads\n");
    //         free(domains);
    //         return;
    //     }
    // }

	long long *pcpuLoads = calloc(npcpus, sizeof(long long));

	for (int i = 0; i < ndomains; i++) {
		// 3. collect vcpu stats
		domain = domains[i];

		nparams = virDomainGetCPUStats(domain, NULL, 0, -1, 1, 0);
		if (nparams < 0)
		{
			fprintf(stderr, "Failed to the nparams for domain %d\n", i);
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
		unsigned char *cpuMap = calloc(1, VIR_CPU_MAPLEN(npcpus));
		result = virDomainGetVcpuPinInfo(domain, 1, cpuMap, VIR_CPU_MAPLEN(npcpus), 0);
		if (result < 0) {
			fprintf(stderr, "Failed to get vcpu pinning info for domain %d\n", i);
			free(params);
			free(cpuMap);
			continue;
		}

		// 3. get pcpu stats
		for (int j = 0; j < nparams; j++) {
			if (strcmp(params[j].field, "cpu_time") == 0) {
				unsigned long long vcpuTime = params[j].value.ul;
				for (int k = 0; k < npcpus; k++) {
					if (VIR_CPU_USED(cpuMap, k)) {
						printf("Domain %d cpu_time: %llu\n", i, vcpuTime);
                        pcpuLoads[k] += vcpuTime;
						break;
                    }
				}
			}
        }

		free(params);
		free(cpuMap);
	}

	// compute & save percentages
	// total pcpu time is eq to the interval... so convert that to nano seconds
	// double *pcpuPercentages = calloc(npcpus, sizeof(double));
	// memset(pcpuPercentages, 0.0, sizeof(double));
	// for (int i = 0; i < npcpus; i++) {
    //     if (prevPcpuLoads[i] != 0) {
    //         long long usage = pcpuLoads[i] > prevPcpuLoads[i] ? (pcpuLoads[i] - prevPcpuLoads[i]) : 0;
	// 		printf("CPU %d.... Prev: %llu, Curr: %llu, Usage: %llu\n", i, prevPcpuLoads[i], pcpuLoads[i], usage);
    //         double usagePercentage = ((double)usage / (interval * 1000000000)) * 100;
    //         pcpuPercentages[i] = usagePercentage;
    //         printf("CPU %d usage: %.2f%%\n", i, usagePercentage);
    //     }
    //     prevPcpuLoads[i] = pcpuLoads[i];
    // }

	for (int i = 0; i < ndomains; i++)
	{
		domain = domains[i];

		// 6. find "best" pcpu to pin vcpu
		int bestPCPU = 0;
		long long minLoad = pcpuLoads[0];

		for (int j = 0; j < npcpus; j++) {
			if (pcpuLoads[j] < minLoad) {
				bestPCPU = j;
				minLoad = pcpuLoads[j];
			}
		}

		printf("Best PCPU: %d\n", bestPCPU);

		if (bestPCPU == -1)
		{
			fprintf(stderr, "No valid pCPU found\n");
			continue;
		}

		// 7. change pcpu assigned to vcpu
		unsigned char *bestPCPUMap = calloc(VIR_CPU_MAPLEN(npcpus), sizeof(unsigned char));
		memset(bestPCPUMap, 0, VIR_CPU_MAPLEN(npcpus));
		bestPCPUMap[bestPCPU / 8] |= (1 << (bestPCPU % 8));

		unsigned char *currentPCPUMap = calloc(VIR_CPU_MAPLEN(npcpus), sizeof(unsigned char));
		result = virDomainGetVcpuPinInfo(domain, 1, currentPCPUMap, VIR_CPU_MAPLEN(npcpus), 0);

		if (result < 0) {
			fprintf(stderr, "Failed to get vcpu pinning info for domain %d\n", i);
			free(bestPCPUMap);
			free(currentPCPUMap);
			continue;
		}

		// substract vcpu time from its soon-to-be prev pcpu
		for (int k = 0; k < npcpus; k++) {
			if (VIR_CPU_USED(currentPCPUMap, k)) {
				for (int j = 0; j < nparams; j++) {
					if (strcmp(params[j].field, "cpu_time") == 0) {
						pcpuLoads[k] -= params[j].value.ul;
						// long long usage = pcpuLoads[k] >= prevPcpuLoads[k] ? pcpuLoads[k] - prevPcpuLoads[k] : 0;
						// double usagePercentage = ((double)usage / (interval * 1000000000)) * 100;
						// pcpuPercentages[k] = usagePercentage;
					}
				}
			}
		}

		result = virDomainPinVcpu(domain, 0, bestPCPUMap, VIR_CPU_MAPLEN(npcpus));
		if (result < 0) {
			fprintf(stderr, "Failed to pin vcpu to pcpu %d for domain %d\n", bestPCPU, i);
		} else {
			// add vcpu time to new pcpu
            for (int j = 0; j < nparams; j++) {
				if (strcmp(params[j].field, "cpu_time") == 0) {
					pcpuLoads[bestPCPU] += params[j].value.ul;
					// long long usage = pcpuLoads[bestPCPU] >= prevPcpuLoads[bestPCPU] ? pcpuLoads[bestPCPU] - prevPcpuLoads[bestPCPU] : 0;
					// double usagePercentage = ((double)usage / (interval * 1000000000)) * 100;
					// pcpuPercentages[bestPCPU] = usagePercentage;
				}
			}
		}

		free(currentPCPUMap);
		free(bestPCPUMap);
	}

	free(pcpuLoads);
	// free(pcpuPercentages);
	free(domains);
}