#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <linux/limits.h>

#define CGROUP_PATH "/sys/fs/cgroup"
#define PROC_CGROUP_PATH "/proc/self/cgroup"

char *allocated_memory = NULL;
int sleep_time_on_signal = 0;

void signal_handler(int sig) {
    printf("\nReceived signal %d. Cleaning up...\n", sig);

    if (allocated_memory) {
        free(allocated_memory);
        printf("Freed allocated memory.\n");
    }

    if (sleep_time_on_signal > 0) {
        printf("Sleeping for %d seconds before exiting...\n", sleep_time_on_signal);
        sleep(sleep_time_on_signal);
    }

    printf("Exiting.\n");
    exit(EXIT_FAILURE);
}

void allocate_memory(size_t memory_to_allocate, int sleep_time_in_seconds) {
    printf("Child: Allocating %zu MiB of memory...\n", memory_to_allocate / (1024 * 1024));
    allocated_memory = malloc(memory_to_allocate);

    if (!allocated_memory) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    memset(allocated_memory, 0, memory_to_allocate);

    printf("Child: Memory allocated and initialized.\n");
    printf("Child: Sleeping for %d seconds...\n", sleep_time_in_seconds);
    sleep(sleep_time_in_seconds);

    free(allocated_memory);
    allocated_memory = NULL;
    printf("Child: Memory freed. Exiting.\n");
}

void enable_controllers(const char *cgroup_path, const char *controllers) {
    char subtree_control_path[PATH_MAX];
    snprintf(subtree_control_path, sizeof(subtree_control_path), "%s/cgroup.subtree_control", cgroup_path);

    FILE *f = fopen(subtree_control_path, "w");
    if (f) {
        fprintf(f, "%s", controllers);
        fclose(f);
    } else {
        perror("Failed to enable controllers in cgroup.subtree_control");
        exit(EXIT_FAILURE);
    }
}

void move_process_to_cgroup(const char *cgroup_path, pid_t pid) {
    char procs_path[PATH_MAX];
    snprintf(procs_path, sizeof(procs_path), "%s/cgroup.procs", cgroup_path);

    FILE *f = fopen(procs_path, "w");
    if (f) {
        fprintf(f, "%d", pid);
        fclose(f);
    } else {
        perror("Failed to move process to cgroup");
        exit(EXIT_FAILURE);
    }
}

void parse_cgroup_path(char *current_cgroup_path, size_t size) {
    FILE *f_cgroup = fopen(PROC_CGROUP_PATH, "r");
    if (!f_cgroup) {
        perror("Failed to open /proc/self/cgroup");
        exit(EXIT_FAILURE);
    }

    // Print the contents of /proc/self/cgroup for debugging
    printf("Parent: Contents of /proc/self/cgroup:\n");
    char line[PATH_MAX];
    while (fgets(line, sizeof(line), f_cgroup)) {
        printf("Parent: %s", line);

        // Parse the line to extract the cgroup path
        // Handle the format: <id>::<path>
        int id;
        char path[PATH_MAX];
        if (sscanf(line, "%d::%s", &id, path) == 2) {
            // For cgroup v2, the controllers field is empty
            if (id == 0) {
                strncpy(current_cgroup_path, path, size - 1);
                current_cgroup_path[size - 1] = '\0'; // Ensure null-termination
                fclose(f_cgroup);
                return;
            }
        }
    }

    fclose(f_cgroup);
    fprintf(stderr, "Error: Failed to parse cgroup path from /proc/self/cgroup\n");
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <memory_in_MiB> <cgroup_mem_limit_in_MiB> <sleep_time_in_seconds>\n", argv[0]);
        return EXIT_FAILURE;
    }

    long memory_in_mib = atol(argv[1]);
    long memory_limit_mib = atol(argv[2]);
    long sleep_time_in_seconds = atol(argv[3]);

    if (memory_in_mib <= 0 || memory_limit_mib <=0 || sleep_time_in_seconds < 0) {
        fprintf(stderr, "Error: Memory size must be positive, and sleep time must be non-negative.\n");
        return EXIT_FAILURE;
    }

    // Convert to bytes
    size_t memory_to_allocate = memory_in_mib * 1024 * 1024;
    size_t memory_limit = memory_limit_mib * 1024 * 1024;

    sleep_time_on_signal = sleep_time_in_seconds;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Parse the current cgroup path this process is running in  
    char current_cgroup_path[PATH_MAX];
    parse_cgroup_path(current_cgroup_path, sizeof(current_cgroup_path));

    // Construct the full path to the parent cgroup
    char parent_cgroup_path[PATH_MAX];
    if (snprintf(parent_cgroup_path, sizeof(parent_cgroup_path), "%s%s", CGROUP_PATH, current_cgroup_path) > PATH_MAX) {
        perror("Parent cgroup path too long");
        exit(EXIT_FAILURE);
    }

    // Create a "controller" cgroup for the parent process
    char controller_cgroup_path[PATH_MAX];
    if (snprintf(controller_cgroup_path, sizeof(controller_cgroup_path), "%s/controller", parent_cgroup_path) > PATH_MAX) {
        perror("Controller cgroup path too long");
        exit(EXIT_FAILURE);
    }

    printf("Parent: Creating controller cgroup directory at: %s\n", controller_cgroup_path);
    if (mkdir(controller_cgroup_path, 0755) != 0 && errno != EEXIST) {
        perror("Failed to create controller cgroup directory");
        exit(EXIT_FAILURE);
    }

    // Move the parent process to the controller cgroup
    move_process_to_cgroup(controller_cgroup_path, getpid());

    // Enable memory and pid controllers in the parent cgroup
    enable_controllers(parent_cgroup_path, "+memory +pids");

    // Create the child cgroup
    char child_cgroup_path[PATH_MAX];
    if (snprintf(child_cgroup_path, sizeof(child_cgroup_path), "%s/htcondor_subjob", parent_cgroup_path) > PATH_MAX) {
        perror("Child cgroup path too long");
        exit(EXIT_FAILURE);
    }

    printf("Parent: Creating child cgroup directory at: %s\n", child_cgroup_path);
    if (mkdir(child_cgroup_path, 0755) != 0 && errno != EEXIST) {
        perror("Failed to create child cgroup directory");
        exit(EXIT_FAILURE);
    }

    // Set the memory limit in the child cgroup
    char mem_limit_path[PATH_MAX];
    if (snprintf(mem_limit_path, sizeof(mem_limit_path), "%s/memory.max", child_cgroup_path) > PATH_MAX) {
        perror("Memory limit path too long");
        exit(EXIT_FAILURE);
    }

    FILE *f_mem = fopen(mem_limit_path, "w");
    if (f_mem) {
        fprintf(f_mem, "%zu", memory_limit);
        fclose(f_mem);
    } else {
        perror("Failed to set memory limit for cgroup");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child process: Move to the child cgroup and allocate memory
        move_process_to_cgroup(child_cgroup_path, getpid());
        allocate_memory(memory_to_allocate, sleep_time_in_seconds);
        exit(EXIT_SUCCESS);
    } else if (pid > 0) {
        // Parent process: Wait for the child to terminate
        wait(NULL);
        printf("Parent: Child process has terminated\n");
    } else {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    }
    printf("Parent: Sleeping for %d seconds...\n", (int)sleep_time_in_seconds);
    sleep(sleep_time_in_seconds);
    printf("Parent: Exiting\n");
    return EXIT_SUCCESS;
}