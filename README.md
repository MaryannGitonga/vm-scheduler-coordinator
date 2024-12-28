# VM CPU Scheduler and Memory Coordinator

This project provides a simple implementation of a [virtual CPU (vCPU)](https://github.com/MaryannGitonga/vm-scheduler-coordinator/tree/main/cpu) scheduler and [memory coordinator](https://github.com/MaryannGitonga/vm-scheduler-coordinator/tree/main/memory) in C, designed to dynamically manage resources for guest virtual machines (VMs). The focus is on balancing CPU workloads across physical CPUs (pCPUs) and managing memory allocation based on VM utilization.

## Overview

The vCPU scheduler and memory coordinator run in the host machine's user space, where they collect utilization statistics for each guest VM through hypervisor calls and take appropriate actions to optimize resource distribution.

- **vCPU Scheduler:** This component tracks each VM's vCPU utilization and dynamically assigns them to pCPUs to maintain balance across all available pCPUs. The goal is to ensure that each pCPU handles a similar workload, maximizing the efficiency of the CPU resources.
  
- **Memory Coordinator:** This component monitors the memory utilization of each guest VM and determines the appropriate amount of free memory to allocate to each VM. The memory coordinator adjusts the memory size and controls the balloon driver to inflate and deflate memory as needed, optimizing memory distribution.

## Tools Used

- **qemu-kvm, libvirt-bin, libvirt-dev**: Packages used to launch virtual machines with KVM and develop programs to manage them.
  
- **libvirt**: A toolkit providing APIs to interact with Linux virtualization:
    - **libvirt-domain**: APIs for monitoring and managing guest VMs.
    - **libvirt-host**: APIs for querying host machine information.
  
- **virsh, uvtool, virt-top, virt-clone, virt-manager, virt-install**: Utilities for managing, cloning, and inspecting virtual machines.
  
- **script**: A command used to record terminal sessions and generate log files.
