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

typedef struct
{
	double usage;
	double prevTime;
	int pinnedPcpu;
} DomainCPUStats;

DomainCPUStats *domainStats = NULL;


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
	int ndomains, result, nvcpus, nparams, npcpus;
	virTypedParameterPtr params;

	// get all active running VMs
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

	double *pcpuUsage = calloc(npcpus, sizeof(double));
	if (domainStats == NULL)
	{
		domainStats = malloc(ndomains * sizeof(DomainCPUStats));
		memset(domainStats, 0, ndomains * sizeof(DomainCPUStats));
	}
	

	for (int i = 0; i < ndomains; i++) {
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

		unsigned char *cpuMap = calloc(1, VIR_CPU_MAPLEN(npcpus));
		result = virDomainGetVcpuPinInfo(domain, 1, cpuMap, VIR_CPU_MAPLEN(npcpus), 0);
		if (result < 0) {
			fprintf(stderr, "Failed to get vcpu pinning info for domain %d\n", i);
			free(params);
			free(cpuMap);
			continue;
		}

		printf("Nvcpus....%d\n", nvcpus);

		for (int j = 0; j < nparams; j++)
		{
			if (strcmp(params[j].field, "vcpu_time") == 0) {
				double vcpuTimeInSeconds = params[j].value.ul / pow(10, 9);
				
				printf("Domain %d vcpu time current %2f prev time %2f\n", vcpuTimeInSeconds, domainStats[i].prevTime);
				if (domainStats[i].prevTime != 0)
				{
					printf("Previous domain %d time is not zero.\n", i);
					double usage = (vcpuTimeInSeconds - domainStats[i].prevTime)/interval;
					domainStats[i].usage = usage;
					for (int k = 0; k < npcpus; k++) {
						if (VIR_CPU_USED(cpuMap, k)) {
							domainStats[i].pinnedPcpu = k;
							pcpuUsage[k] += domainStats[i].usage;
							printf("Domain %d is pinned to cpu %d\n", i, k);
						}
					}
				}
				domainStats[i].prevTime = vcpuTimeInSeconds;
				break;
			}
		}

		printf("pcpu: %d aos_vm_%d usage: %.2f\n", domainStats[i].pinnedPcpu, i + 1, domainStats[i].usage);
		

		free(params);
		free(cpuMap);
	}

	// double targetUsagePerPcpu = totalCpuUsage / npcpus;
	// printf("Total usage: %.2f\n", totalCpuUsage);
	// printf("Target usage per pcpu: %.2f\n", targetUsagePerPcpu);

	// int pcpusBalanced = 1;
    // for (int i = 0; i < npcpus; i++) {
	// 	printf("pcpu %d usage: %.2f\n", i, pcpuUsage[i]);
    //     // if (fabs(pcpuUsage[i] - targetUsagePerPcpu) > 0.1) {
    //     //     pcpusBalanced = 0;
    //     //     break;
    //     // }
    // }

    // if (pcpusBalanced) {
    //     printf("pCPUs are balanced. No remapping required.\n");
    //     free(pcpuUsage);
    //     free(vcpuUsage);
    //     free(domains);
    //     return;
    // }

	// printf("pCPUs are not balanced. Remapping vCPUs...\n");

	// for (int i = 0; i < ndomains; i++) {
    //     domain = domains[i];
	// 	int leastLoadedPcpu = -1;
    //     unsigned char *newCpuMap = calloc(1, VIR_CPU_MAPLEN(npcpus));
	// 	unsigned char *cpuMap = calloc(1, VIR_CPU_MAPLEN(npcpus));
	// 	result = virDomainGetVcpuPinInfo(domain, 1, cpuMap, VIR_CPU_MAPLEN(npcpus), 0);
	// 	if (result < 0) {
	// 		fprintf(stderr, "Failed to get vcpu pinning info for domain %d\n", i);
	// 		continue;
	// 	}
		
    //     for (int j = 0; j < npcpus; j++) {
    //         if (pcpuUsage[j] < 1 && !VIR_CPU_USED(cpuMap, j)) {
	// 			double predictedLoad = pcpuUsage[j] + vcpuUsage[i];
	// 			if (predictedLoad < 1)
	// 			{
	// 				leastLoadedPcpu = j;
	// 				newCpuMap[j / 8] |= (1 << (j % 8));
	// 			}
    //         }
    //     }

	// 	if (leastLoadedPcpu != -1)
	// 	{
	// 		result = virDomainPinVcpu(domain, 0, newCpuMap, VIR_CPU_MAPLEN(npcpus));
	// 		if (result < 0) {
	// 			fprintf(stderr, "Failed to pin vcpus for domain %d\n", i);
	// 		}
	// 	}
    //     free(newCpuMap);
    // }

	free(pcpuUsage);
	free(domains);
}