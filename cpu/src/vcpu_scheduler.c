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
	int ndomains, result, nparams;
	virTypedParameterPtr params;

	// 2. get all active running VMs
	ndomains = virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_RUNNING);

	if(ndomains < 0){
		fprintf(stderr, "Failed to get active running VMs\n");
		return;
	}

	for (int i = 0; i < ndomains; i++)
	{
		// 3. collect vcpu stats
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

		for (int j = 0; j < nparams; j++) {
			if (params[j].field === "cpu_time")
			{
				switch (params[j].type) {
					case VIR_TYPED_PARAM_INT:
						printf("  Value int: %d\n", params[j].value.i);
						break;
					case VIR_TYPED_PARAM_UINT:
						printf("  Value uint: %u\n", params[j].value.ui);
						break;
					case VIR_TYPED_PARAM_LLONG:
						printf("  Value llong: %lld\n", params[j].value.l);
						break;
					case VIR_TYPED_PARAM_ULLONG:
						printf("  Value ullong: %llu\n", params[j].value.ul);
						break;
					case VIR_TYPED_PARAM_STRING:
						printf("  Value string: %s\n", params[j].value.s);
						break;
					default:
						printf("  Value: (unknown type)\n");
						break;
            	}
			}
            
        }

        free(params);

		// 5. determine map affinity

		// 6. find "best" pcpu to pin vcpu

		// 7. change pcpu assigned to vcpu
	}

	free(domains);
}




