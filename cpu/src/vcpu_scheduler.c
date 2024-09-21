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
	double *prevTimes;
	int pinnedPcpu;
} DomainCPUStats;

double get_standard_deviation(double *values, int nvalues) {
	// Calculate mean
	double mean = 0;
	for (int i = 0; i < nvalues; i++) {
		mean += values[i];
	}

	mean = mean / nvalues;

	double sumSquareDiffs = 0;
	for (int i = 0; i < nvalues; i++) {
		sumSquareDiffs += (values[i] - mean) * (values[i] - mean);
	}

	double meanSquareDiff = sumSquareDiffs / nvalues;
	double stdev = sqrt(meanSquareDiff);
	return stdev;
}

int are_cpus_balanced(double *cpuUsages, int ncpus) {
	double stddev = get_standard_deviation(cpuUsages, ncpus);
	return stddev <= 0.05;
}

int DomainCPUStats_initialize_for_domain(DomainCPUStats *domainStats, int npcpus) {
	domainStats->prevTimes = calloc(npcpus, sizeof(double));
	if (domainStats->prevTimes == NULL) {
		return -1;
	}

	return 0;
}

void DomainCPUStats_deinitialize_for_domain(DomainCPUStats *domainStats) {
	if (domainStats->prevTimes != NULL) {
		free(domainStats->prevTimes);
	}
}

void DomainCPUStats_free(DomainCPUStats *stats, int ndomains) {
	for (int i = 0; i < ndomains; i++) {
		DomainCPUStats_deinitialize_for_domain(&stats[i]);
	}

	free(stats);
}

DomainCPUStats* DomainCPUStats_create(int ndomains, int ncpus) {
	DomainCPUStats *stats = calloc(ndomains, sizeof(DomainCPUStats));
	if (stats == NULL) {
		goto error;
	}

	for (int i = 0; i < ndomains; i++) {
		if (DomainCPUStats_initialize_for_domain(&stats[i], ncpus) == -1)
		{
			goto error;
		}
	}

	return stats;

error:
	printf("Failed to create domain stats\n");
	DomainCPUStats_free(stats, ndomains);
	return NULL;
}

DomainCPUStats *domainStats = NULL;
int ndomains;
int first_iteration = 1;


int is_exit = 0; // DO NOT MODIFY THIS VARIABLE

void CPUScheduler(virConnectPtr conn,int interval);

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
	printf("Caught Signal");
	DomainCPUStats_free(domainStats, ndomains);
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
	int result, nparams, npcpus;
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
		domainStats = DomainCPUStats_create(ndomains, npcpus);
	}
	

	for (int i = 0; i < ndomains; i++) {
		domain = domains[i];

		// nvcpus = virDomainGetCPUStats(domain, NULL, 0, 0, 0, 0);

		nparams = virDomainGetCPUStats(domain, NULL, 0, 0, 1, 0);
		if (nparams < 0)
		{
			fprintf(stderr, "Failed to the nparams for domain %d\n", i);
			continue;
		}

		params = calloc(npcpus * nparams, sizeof(virTypedParameter));
		result = virDomainGetCPUStats(domain, params, nparams, 0, npcpus, 0);
		if (result < 0)
		{
			fprintf(stderr, "Failed to get vcpu stats for domain %d\n", i);
			free(params);
			continue;
		}

		for (int j = 0; j < npcpus; j++)
		{
			for (int k = 0; k < nparams; k++)
			{
				int p = (j * nparams) + k;
				if (strcmp(params[p].field, "vcpu_time") == 0) {
					double vcpuTimeInSeconds = params[p].value.ul / pow(10, 9);
					printf("Domain %d vcpu_time %2f seconds on cpu  %d\n", i, vcpuTimeInSeconds, j);
					printf("Domain %d prev Time %2f seconds on cpu  %d\n", i, domainStats[i].prevTimes[j], j);
					double timeDiff = vcpuTimeInSeconds - domainStats[i].prevTimes[j];
					printf("Domain %d time diff on cpu %d %2f seconds\n", i, k, timeDiff);
					pcpuUsage[j] += timeDiff;
					domainStats[i].prevTimes[j] = vcpuTimeInSeconds;
				}
			}
		}

		// printf("Domain %d vcpu time current %2f prev time %2f\n", i, vcpuTimeInSeconds, domainStats[i].prevTime);
		// double timeDiff = (vcpuTimeInSeconds - domainStats[i].prevTime);
		// printf("VCPU time diff on domain %d, %2f, interval %d\n", i, timeDiff, interval);
		// if (domainStats[i].prevTime != 0)
		// {
		// 	printf("Previous domain %d time is not zero.\n", i);
		// 	double usage = timeDiff/interval;
		// 	domainStats[i].usage = usage;
		// 	// domainStats[i].pinnedPcpu = j;
		// 	// pcpuUsage[j] += domainStats[i].usage;
		// 	// printf("Domain %d is pinned to cpu %d\n", i, j);
		// }
		// domainStats[i].prevTime = vcpuTimeInSeconds;
		

		// for (int j = 0; j < nparams; j++)
		// {
		// 	printf("Domain %d has param %s\n", i, params[j].field);
		// 	if (strcmp(params[j].field, "vcpu_time") == 0) {
		// 		double vcpuTimeInSeconds = params[j].value.ul / pow(10, 9);
				
		// 		printf("Domain %d vcpu time current %2f prev time %2f\n", i, vcpuTimeInSeconds, domainStats[i].prevTime);
		// 		if (domainStats[i].prevTime != 0)
		// 		{
		// 			printf("Previous domain %d time is not zero.\n", i);
		// 			double usage = (vcpuTimeInSeconds - domainStats[i].prevTime)/interval;
		// 			domainStats[i].usage = usage;
		// 			for (int k = 0; k < npcpus; k++) {
		// 				if (VIR_CPU_USED(cpuMap, k)) {
		// 					domainStats[i].pinnedPcpu = k;
		// 					pcpuUsage[k] += domainStats[i].usage;
		// 					printf("Domain %d is pinned to cpu %d\n", i, k);
		// 				}
		// 			}
		// 		}
		// 		domainStats[i].prevTime = vcpuTimeInSeconds;
		// 		break;
		// 	}
		// }

		// printf("pcpu: %d aos_vm_%d usage: %.2f\n", domainStats[i].pinnedPcpu, i + 1, domainStats[i].usage);
		

		free(params);
		free(cpuMap);
	}

	if (first_iteration) {
		printf("First iteration, skip balancing...");
		first_iteration = 0;
		goto done;
	}

	for (int i = 0; i < npcpus; i++) {
		printf("CPU %d overall usage time in last cycle is %2f seconds\n", i, pcpuUsage[i]);
		pcpuUsage[i] = pcpuUsage[i] / interval;
		printf("CPU %d normalized usage is %2f\n", i, pcpuUsage[i]);
	}

	int balanced = are_cpus_balanced(pcpuUsage, npcpus);
	
	if(balanced){
		goto done;
	}

	// unsigned char *cpuMap = calloc(1, VIR_CPU_MAPLEN(npcpus));
	// result = virDomainGetVcpuPinInfo(domain, 1, cpuMap, VIR_CPU_MAPLEN(npcpus), 0);
	// if (result < 0) {
	// 	fprintf(stderr, "Failed to get vcpu pinning info for domain %d\n", i);
	// 	free(params);
	// 	free(cpuMap);
	// 	continue;
	// }


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
done:
	free(pcpuUsage);
	free(domains);
}