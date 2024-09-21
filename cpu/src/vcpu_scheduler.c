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
	unsigned char cpumap;
	int pinnedPcpu;
} DomainCPUStats;

double get_standard_deviation(double *values, int nvalues, double *mean_out) {
	// Calculate mean
	double mean = 0;
	for (int i = 0; i < nvalues; i++) {
		mean += values[i];
	}

	mean = mean / nvalues;

	if (mean_out != NULL) {
		*mean_out = mean;
	}

	double sumSquareDiffs = 0;
	for (int i = 0; i < nvalues; i++) {
		sumSquareDiffs += (values[i] - mean) * (values[i] - mean);
	}

	double meanSquareDiff = sumSquareDiffs / nvalues;
	double stdev = sqrt(meanSquareDiff);
	return stdev;
}

int get_max_item_index(double *values, int nvalues) {
	if (nvalues == 0) {
		return -1;
	}

	if (nvalues == 1) {
		return 0;
	}

	double maxValue = values[0];
	int maxIndex = 0;
	for (int i = 1; i < nvalues; i++) {
		if (values[i] > maxValue) {
			maxValue = values[i];
			maxIndex = i;
		}
	}

	return maxIndex;
}

int are_cpus_balanced(double *cpuUsages, int ncpus, double *mean_out, double *stddev_out) {
	double stddev = get_standard_deviation(cpuUsages, ncpus, mean_out);
	printf("Standard dev: %2f\n", stddev);
	if (stddev_out != NULL) {
		*stddev_out = stddev;
	}
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
	double *domainUsage = calloc(ndomains, sizeof(double));
	if (domainStats == NULL)
	{
		domainStats = DomainCPUStats_create(ndomains, npcpus);
	}
	

	for (int i = 0; i < ndomains; i++) {
		domain = domains[i];

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
		
		unsigned char cpumap = 0;
		result = virDomainGetVcpuPinInfo(domain, 1, &cpumap, 1, 0);
		if (result < 0) {
			fprintf(stderr, "Failed to get cpumap for domain %d\n", i);
			free(params);
			goto done;
		}

		domainStats[i].cpumap = cpumap;

		for (int j = 0; j < npcpus; j++)
		{
			for (int k = 0; k < nparams; k++)
			{
				int p = (j * nparams) + k;
				if (strcmp(params[p].field, "vcpu_time") == 0) {
					double vcpuTimeInSeconds = params[p].value.ul / pow(10, 9);
					double timeDiff = vcpuTimeInSeconds - domainStats[i].prevTimes[j];
					pcpuUsage[j] += timeDiff;
					domainUsage[i] += timeDiff;

					domainStats[i].prevTimes[j] = vcpuTimeInSeconds;
				}
			}
		}

		domainUsage[i] = domainUsage[i]/interval;
		printf("Domain %d overall usage is %2f\n", i, domainUsage[i]);
		free(params);
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

	double meanUsage = 0;
	double stddevUsage = 0;
	int balanced = are_cpus_balanced(pcpuUsage, npcpus, &meanUsage, &stddevUsage);

	printf("Balanced: %d\n", balanced);

	if(balanced){
		goto done;
	}

	printf("pcpus not balanced. Mean usage %2f, stddev %2f\n", meanUsage, stddevUsage);

	// for (int i = 0; i < npcpus; i++)
	// {
	// 	if(meanUsage - pcpuUsage[i] > 0.1) {
	// 		printf("PCPU %d is underutilized, usage is %2f, attempt to balance...\n", i, pcpuUsage[i]);
	// 		// CPU usage is low compared to the mean
	// 		// find CPU with most usage to donate workloads to the underutilized cpu
	// 		int busiestCpu = get_max_item_index(pcpuUsage, npcpus);
	// 		printf("PCPU %d is busiest\n", busiestCpu);
	// 		for (int d = 0; d < ndomains; d++) {
	// 			if (VIR_CPU_USED(&domainStats[d].cpumap, busiestCpu) && !VIR_CPU_USED(&domainStats[d].cpumap, i)) {
	// 				unsigned char newCpuMap = 1 << i;
	// 				result = virDomainPinVcpu(domains[d], 0, &newCpuMap, VIR_CPU_MAPLEN(npcpus));
	// 				if (result < 0) {
	// 					fprintf(stderr, "Failed to pin vcpus to domain %d\n", d);
	// 					goto done;
	// 				}

	// 				printf("Moved domain %d from pcpu %d to pcpu %d\n", d, busiestCpu, i);
	// 				break;
	// 			}
	// 		}

	// 		break;
	// 	}
	// }

	double *plannedUsage = calloc(npcpus, sizeof(double));
	unsigned char *newCpuMappings = calloc(ndomains, sizeof(unsigned char));
	// to balance the cpus we attempt to find new pin mappings from scratch
	// such that workloads will be distributed evenly
	// Any domain can be repinned to any PCPU. We use the mean usage
	// as the target usage, and for each CPU, we find the domains that will
	// get it to closest to the target usage
	// for each pcpu, start with planned usage = 0
	// find domain that will it get closer to the target, assign that to pcpu
	// increase planned usage, repeat until planned usage = target usage
	for (int c = 0; c < npcpus; c++) {
		double remainingUsage = meanUsage - plannedUsage[c];
		while (remainingUsage > 0) {
			int bestDomain = -1;
			for (int d = 0; d < ndomains; d++) {
				// domain to assign must be free (not assigned to any pcpu)
				// and usage must not be greater than the remaining target usage
				if (newCpuMappings[d] == 0 && domainUsage[d] <= remainingUsage) {
					// initializing the bestDomain inside this if-block
					// ensures that the best domain always meets our constraints
					if (bestDomain == -1) {
						bestDomain = d;
						continue;
					}

					// of all possible domains that meet our constraints
					// prefer the one with the most usage
					if (domainUsage[d] > domainUsage[bestDomain]) {
						bestDomain = d;
					}
				}
			}

			if (bestDomain > -1) {
				newCpuMappings[bestDomain] = 1 << c;
				remainingUsage -= domainUsage[bestDomain];
				printf("Assigning domain %d with usage %2f to pcpu %d\n", bestDomain, domainUsage[bestDomain], c);
			} else {
				break;
			}
		}
	}

	printf("Assigning planned mappings to domains\n");
	for (int d = 0; d < ndomains; d++) {
		unsigned char domainCpuMap = newCpuMappings[d];
		printf("New CPU mapping for domain %d %d\n", d, domainCpuMap);

		result = virDomainPinVcpu(domains[d], 0, &domainCpuMap, VIR_CPU_MAPLEN(npcpus));
		if (result < 0) {
			fprintf(stderr, "Failed to pin vcpus to domain %d\n", d);
			goto done;
		}
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
	// free(cpuMap);
done:
	free(pcpuUsage);
	free(domains);
}