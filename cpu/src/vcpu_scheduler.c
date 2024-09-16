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
double *prevPcpuLoads = NULL;

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
	free(prevPcpuLoads);
	virConnectClose(conn);
	return 0;
}

/* COMPLETE THE IMPLEMENTATION */
void CPUScheduler(virConnectPtr conn, int interval)
{
	printf("Scheduler started...\n");
	virDomainPtr *domains, domain;
	int ndomains, result, nvcpus, nparams, npcpus;
	virTypedParameterPtr params;

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

	if (prevPcpuLoads == NULL) {
        prevPcpuLoads = calloc(npcpus, sizeof(double));
        if (prevPcpuLoads == NULL) {
            fprintf(stderr, "Failed to allocate memory for prevPcpuLoads\n");
            free(domains);
            return;
        }
    }

	double *pcpuLoads = calloc(npcpus, sizeof(double));
	double *pcpuUsage = calloc(npcpus, sizeof(double));
	double *domainLoads = calloc(8, sizeof(double));

	for (int i = 0; i < ndomains; i++) {
		// 3. collect vcpu stats
		domain = domains[i];

		nvcpus = virDomainGetCPUStats(domain, NULL, 0, 0, 0, 0);

		nparams = virDomainGetCPUStats(domain, NULL, 0, 0, 1, 0);
		if (nparams < 0)
		{
			fprintf(stderr, "Failed to the nparams for domain %d\n", i);
			continue;
		}

		params = calloc(nvcpus * nparams, sizeof(virTypedParameter));
		result = virDomainGetCPUStats(domain, params, nparams, 0, nvcpus, 0);
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
			if (strcmp(params[j].field, "vcpu_time") == 0) {
				double vcpuTime = params[j].value.ul / pow(10, 9);
				domainLoads[i] = vcpuTime;
				for (int k = 0; k < npcpus; k++) {
					if (VIR_CPU_USED(cpuMap, k)) {
						printf("aos_vm_%d on pcpu %d vcpu_time: %.2f%%\n", i + 1, k, vcpuTime);
                        pcpuLoads[k] += vcpuTime;
                    }
				}
			}
        }

		free(params);
		free(cpuMap);
	}

	// compute & save usage
	// total pcpu time is eq to the interval.
	for (int i = 0; i < npcpus; i++) {
        if (prevPcpuLoads[i] != 0) {
            double usage = (pcpuLoads[i] > prevPcpuLoads[i] ? (pcpuLoads[i] - prevPcpuLoads[i]) : 0);
			printf("CPU %d.... Prev: %.2f%%, Curr: %.2f%%, Usage: %.2f%%\n", i, prevPcpuLoads[i], pcpuLoads[i], usage);
            double usageNormalized = (usage / (interval));
            printf("CPU %d usage: %.2f%%\n", i, usageNormalized);
			pcpuUsage[i] += usageNormalized;
        }
        prevPcpuLoads[i] = pcpuLoads[i];
    }

	// for (int i = 0; i < ndomains; i++)
	// {
	// 	domain = domains[i];

	// 	// 6. find "best" pcpu to pin vcpu
	// 	int bestPCPU = -1;
	// 	double minUsage = 1;
	// 	double predictedUsage = interval; // any number that's larger than required usage (1)

	// 	unsigned char *currCpuMap = calloc(1, VIR_CPU_MAPLEN(npcpus));
	// 	result = virDomainGetVcpuPinInfo(domain, 1, currCpuMap, VIR_CPU_MAPLEN(npcpus), 0);
	// 	if (result < 0) {
	// 		fprintf(stderr, "Failed to get vcpu pinning info for domain %d\n", i);
	// 		free(currCpuMap);
	// 		continue;
	// 	}

	// 	for (int j = 0; j < npcpus; j++) {
	// 		printf("PCPU %d... usage: %llu... min-usage:%llu\n", j, pcpuUsage[j], minUsage);

	// 		if (!VIR_CPU_USED(currCpuMap, j)) {
	// 			predictedUsage = ((pcpuLoads[j] + domainLoads[i]) - prevPcpuLoads[j]) / interval;
	// 		}
			
	// 		if (pcpuUsage[j] < minUsage && predictedUsage < 1) {
	// 			bestPCPU = j;
	// 			minUsage = pcpuUsage[j];
	// 		}
	// 	}

	// 	printf("Best PCPU: %d\n", bestPCPU);

	// 	if (bestPCPU == -1)
	// 	{
	// 		fprintf(stderr, "No valid pCPU found\n");
	// 		continue;
	// 	}

	// 	// 7. change pcpu assigned to vcpu
	// 	unsigned char *bestPCPUMap = calloc(VIR_CPU_MAPLEN(npcpus), sizeof(unsigned char));
	// 	memset(bestPCPUMap, 0, VIR_CPU_MAPLEN(npcpus));
	// 	bestPCPUMap[bestPCPU / 8] |= (1 << (bestPCPU % 8));

	// 	unsigned char *currentPCPUMap = calloc(VIR_CPU_MAPLEN(npcpus), sizeof(unsigned char));
	// 	result = virDomainGetVcpuPinInfo(domain, 1, currentPCPUMap, VIR_CPU_MAPLEN(npcpus), 0);

	// 	if (result < 0) {
	// 		fprintf(stderr, "Failed to get vcpu pinning info for domain %d\n", i);
	// 		free(bestPCPUMap);
	// 		free(currentPCPUMap);
	// 		continue;
	// 	}

	// 	// substract vcpu time from its soon-to-be prev pcpu
	// 	for (int k = 0; k < npcpus; k++) {
	// 		if (VIR_CPU_USED(currentPCPUMap, k)) {
	// 			for (int j = 0; j < nparams; j++) {
	// 				if (strcmp(params[j].field, "cpu_time") == 0) {
	// 					pcpuLoads[k] -= params[j].value.ul;
	// 					// long long usage = pcpuLoads[k] >= prevPcpuLoads[k] ? pcpuLoads[k] - prevPcpuLoads[k] : 0;
	// 					// double usagePercentage = ((double)usage / (interval * 1000000000)) * 100;
	// 					// pcpuPercentages[k] = usagePercentage;
	// 				}
	// 			}
	// 		}
	// 	}

	// 	result = virDomainPinVcpu(domain, 0, bestPCPUMap, VIR_CPU_MAPLEN(npcpus));
	// 	if (result < 0) {
	// 		fprintf(stderr, "Failed to pin vcpu to pcpu %d for domain %d\n", bestPCPU, i);
	// 	} else {
	// 		// add vcpu time to new pcpu
    //         for (int j = 0; j < nparams; j++) {
	// 			if (strcmp(params[j].field, "cpu_time") == 0) {
	// 				pcpuLoads[bestPCPU] += params[j].value.ul;
	// 				// long long usage = pcpuLoads[bestPCPU] >= prevPcpuLoads[bestPCPU] ? pcpuLoads[bestPCPU] - prevPcpuLoads[bestPCPU] : 0;
	// 				// double usagePercentage = ((double)usage / (interval * 1000000000)) * 100;
	// 				// pcpuPercentages[bestPCPU] = usagePercentage;
	// 			}
	// 		}
	// 	}

	// 	free(currentPCPUMap);
	// 	free(bestPCPUMap);
	// }

	free(pcpuLoads);
	free(pcpuUsage);
	free(domains);
}