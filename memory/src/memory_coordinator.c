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
	int ndomains, result, nparams, npcpus;
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

	for (int i = 0; i < ndomains; i++)
	{
		domain = domains[i];
		result = virDomainSetMemoryStatsPeriod(domain, interval, 0);
		if (result != 0)
		{
			fprintf(stderr, "Failed to set memory stats period.\n");
		}

		// get memory params
		// virDomainGetMemoryParameters
		result = virDomainGetMemoryParameters(domain, NULL, &nparams, 0);
		if (result != 0)
		{
			fprintf(stderr, "Failed to get nparams.\n");
		}

		params = malloc(sizeof(*params) * nparams);
        if (params == NULL) {
            fprintf(stderr, "Failed to allocate memory for params.\n");
            continue;
        }
        memset(params, 0, sizeof(*params) * nparams);

		result = virDomainGetMemoryParameters(domain, params, &nparams, 0);
		if(result != 0){
			fprintf(stderr, "Failed to get memory params.\n");
		}

		for (int j = 0; j < nparams; j++)
		{
			printf("  %s: ", params[j].field);
            switch (params[j].type)
            {
                case VIR_TYPED_PARAM_INT:
                    printf("%d\n", params[j].value.i);
                    break;
                case VIR_TYPED_PARAM_UINT:
                    printf("%u\n", params[j].value.ui);
                    break;
                case VIR_TYPED_PARAM_LLONG:
                    printf("%lld\n", params[j].value.l);
                    break;
                case VIR_TYPED_PARAM_ULLONG:
                    printf("%llu\n", params[j].value.ul);
                    break;
                case VIR_TYPED_PARAM_DOUBLE:
                    printf("%f\n", params[j].value.d);
                    break;
                case VIR_TYPED_PARAM_BOOLEAN:
                    printf("%s\n", params[j].value.b ? "true" : "false");
                    break;
                case VIR_TYPED_PARAM_STRING:
                    printf("%s\n", params[j].value.s);
                    break;
                default:
                    printf("Unknown type\n");
                    break;
            }
		}
		
		

		// virDomainMemoryStats

		// virDomainSetMemory
		
	}
	
}
