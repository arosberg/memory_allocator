# Memory Allocator with Cgroup Management

## Overview
`memory_allocator` is a program that spawns a child process, assigns it to a cgroup with a memory limit, and makes the child process allocate a specified amount of memory. The process then sleeps for a given duration before exiting. This program demonstrates cgroup-based memory management in Linux.

## Features
- Creates a child process and assigns it to a dedicated cgroup.
- Enforces a memory limit on the child process.
- Allows the child process to allocate a user-defined amount of memory.
- Supports a configurable sleep duration before exiting.
- Handles cleanup and signal interruptions gracefully.

## Requirements
- Linux system with cgroup v2 enabled.
- Root privileges (or appropriate cgroup permissions).

## Usage
```sh
./memory_allocator <memory_in_MiB> <cgroup_mem_limit_in_MiB> <sleep_time_in_seconds>
```

### Arguments
- `<memory_in_MiB>`: Amount of memory (in MiB) that the child process should allocate.
- `<cgroup_mem_limit_in_MiB>`: Memory limit (in MiB) for the cgroup.
- `<sleep_time_in_seconds>`: Duration (in seconds) the child process should sleep before exiting.

### Example
```sh
sudo ./memory_allocator 100 200 10
```
This command:
- Spawns a child process that allocates 100 MiB of memory.
- Places the child in a cgroup with a 200 MiB memory limit.
- Keeps the child process running for 10 seconds before cleanup.

## Signal Handling
- The program handles `SIGINT` and `SIGTERM`.
- On receiving a signal, it frees allocated memory and sleeps before exiting.

## Implementation Details
- Parses the current cgroup path from `/proc/self/cgroup`.
- Moves the parent process to the controller cgroup.
- Creates a cgroup for the child process with a specified memory limit.
- Enables `memory` and `pids` controllers for the child process cgroup.
- Moves the child process to the new cgroup and enforces limits.
- Waits for the child to terminate before the parent exits.

## License
This project is released under the MIT License.

