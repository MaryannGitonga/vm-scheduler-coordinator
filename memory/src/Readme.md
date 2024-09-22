# Memory Coordinator

## Memory Allocation and Reclamation Policy

### Key terms
- **Starving VM**: A VM whose unused memory is decreasing significantly (more than 10MB decrease) while its actual memory usage is greater than initial memory allocated (the last part is based on the test cases).
- **Sacrificial VM**: A VM that is not part of the starving VMs and has been targeted by the coordinator to provide memory to a starving VM.
- **Bloated VM**: A VM that has attained maximum limit of memory allowed and the actual memory is more than the initial memory allocated.
- **Host Free Memory**: The free memory available on the host system which can be allocated to starving VMs when necessary.

### Policy

The policy consists of the following steps:
1. **Memory stats collection**:
- For each VM, the memory stats such as actual memory usage, unused memory, and maximum memory limit are collected.
- These values are stored globally and are used to track changes in every coordinator call per interval.

2. **Identify starving VMs**:
- There is an array that keeps track of starving VMs across the cycles. The number of the starving VMs is also tracked separately for ease in implementation.
- A VM is marked as "starving" if:
    - Its previous unused memory is greater than zero.
    - Its unused memory is below the threshold (100MB).
    - There is a decrease (`previous_unused - current_unused`) in unused memory.
    - Its actual memory usage is more than a VM's initial memory (if it's less, the assumption is that it was previously sacrificed).

3. **Memory reallocation process**:
- For each starving VM:
    1. **Identify a sacrificial VM**:
    - A VM can be sacrificed if it has:
        - Unused memory of at least 100 MB.
        - Actual memory greater than 200 MB.
    - To decide how much memory to release and give to the starving VM:
      - The coordinator only takes memory from the sacrificed VM if the VM has more than a minimum threshold (lowerBoundMemory). The amount taken is the lesser of the memory above that threshold or the amount the starving VM needs.
      - The memory given to the starving VM is capped by how much more it can take without exceeding its maximum memory limit.
    2. **Allocate memory from the host**:
    - If no VM can be sacrificed and the host has sufficient free memory (more than 200 MB), allocate memory from the host.
    - The same algorithm applied in sacrificed VMs is used to decide on how much memory to release from the host.
    3. **Final attempt**:
    - If neither sacrificing a VM nor allocating from the host is possible, the starving VM is marked as "readyToRelease", indicating that it has attained maximum possible memory and cannot receive additional memory.

4. **Memory reclamation**:
- A VM is marked as "bloated"/ with a "readyToRelease" designator if:
    - Its unused memory had a spike (more then 200MB increase) indicating that the program that was running on it is terminated.
    - Its actual memory is at maximum limit (2048MB).
- When a starving VM has reached its maximum memory limit:
   - Memory is reclaimed back to its initial memory allocated.

### Function workflow

1. **Collect running VMs**: 
   - The coordinator gathers all active VMs.
2. **Gather memory stats**: 
   - The coordinator collects each domain's memory usage stats and updates their structs for memory tracking.
3. **Identify starving VMs**: 
   - Identify VMs that are starving based on changes in their unused memory and current actual memory usage.
4. **Identify starving VMs with terminated programs**: 
   - Identify starving VMs with terminated programs based on if there's spike in their unused memory.
5. **Reallocate memory**:
   - Attempt to release memory from non-starving VMs and allocate it to starving VMs.
   - If no VMs can be sacrificed, allocate memory from the host's free memory.
6. **Memory reclamation**:
   - When starving VMs reach their maximum memory limit, memory is gradually reclaimed to ensure they don't hold more memory than required.

### Limitations
- The policy uses predefined thresholds (e.g., spike of 200MB or more to conclude that a program in a starving VM is terminated) that may need tuning based on specific VM workloads and host configurations.