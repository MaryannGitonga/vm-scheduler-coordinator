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
- There is an array that keeps track of starving VMs across the cycles. The number of the starving VMs is also tracked separately for ease in implemntation.
- A VM is marked as "starving" if:
    - Its previous unused memory is greater than zero.
    - The decrease (`previous_unused - current_unused`) in unused memory is significant (more than 10 MB).
    - Its actual memory usage is not less than a VM's initial memory (if it's less, the assumption is that it was previously sacrificed).

3. **Memory reallocation process**:
- For each starving VM:
    1. **Identify a sacrificial VM**:
    - If not all VMs are starving (test case 2), attempt to find a "sacrificial" VM to release memory. 
    - A VM can be sacrificed if it has:
        - Unused memory of at least 100 MB.
        - Actual memory greater than 200 MB.
    - Release memory from the sacrificial VM, ensuring it doesn't drop below 200 MB. At most 104MB of memory is released at a go.
    2. **Allocate memory from the host**:
    - If no VM can be sacrificed and the host has sufficient free memory (more than 200 MB), allocate memory from the host. At most 104MB of memory is released at a go.
    3. **Final attempt**:
    - If neither sacrificing a VM nor allocating from the host is possible, the starving VM is marked as having "attained max", indicating that it has attained maximum possible memory and cannot receive additional memory.

4. **Memory reclamation**:
   - When a starving VM has reached its maximum memory limit:
     - Memory is reclaimed back to its initial memory allocated.

### Summary
- If a VM's unused memory is rapidly decreasing while it's using all of its actual memory, it will be marked as "starving."
- Memory is then reallocated to this VM from other VMs (that are not starving) or from the host's free memory if available.
- If no memory can be allocated, the VM is marked as having "attained max," preventing further attempts to allocate memory to it until its state changes.
- This policy ensures efficient use of memory across all VMs while preventing any single VM (or host) from being deprived of the necessary memory resources.

## Function workflow

1. **Collect Running VMs**: 
   - The scheduler gathers all active VMs and sets memory statistics collection intervals.
2. **Memory Stats Gathering**: 
   - For each VM, collect memory usage statistics and update the internal memory tracking data structures.
3. **Identify and Mark Starving VMs**: 
   - Identify VMs that are starving based on changes in their unused memory and current actual memory usage.
4. **Reallocate Memory**:
   - Attempt to release memory from non-starving VMs and allocate it to starving VMs.
   - If no VMs can be sacrificed, allocate memory from the host's free memory.
5. **Memory Reclamation**:
   - When starving VMs reach their maximum memory limit, memory is gradually reclaimed to ensure they don't hold more memory than required.
6. **Print and Log Status**:
   - Print memory statuses, actions taken (e.g., reallocations), and the current number of starving VMs for monitoring.

## Edge cases
- **All VMs Starving**: If all VMs are marked as starving, the scheduler focuses on using the host's free memory first before indicating that no more memory can be allocated.
- **Insufficient Host Memory**: If the host does not have enough free memory, the scheduler attempts to reclaim memory from the starving VMs themselves.

## Limitations
- The policy uses predefined thresholds (e.g., 10MB decrease to conclude that a VM is "starving") that may need tuning based on specific VM workloads and host configurations.