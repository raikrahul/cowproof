#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdint.h>

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__ (
        "lfence\n\t"
        "rdtsc\n\t"
        "lfence\n\t"
        : "=a"(lo), "=d"(hi)
        :: "memory"
    );
    return ((uint64_t)hi << 32) | lo;
}

int main() {
    size_t size = 4096;
    char *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    /* Fault it in the parent so it's physically allocated */
    p[0] = 'A';

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* Child */
        uint64_t t1, t2;
        volatile char tmp;

        /* Test 1: Read a CoW page */
        t1 = rdtsc();
        tmp = p[0]; /* Read, should be completely free, no fault */
        t2 = rdtsc();
        printf("[Child] Read CoW page cost: %lu cycles (char: %c)\n", t2 - t1, tmp);

        /* Test 2: Write to CoW page */
        t1 = rdtsc();
        p[0] = 'B'; /* Write, triggers page fault and CoW */
        t2 = rdtsc();
        printf("[Child] Write CoW page cost: %lu cycles\n", t2 - t1);

        exit(0);
    } else {
        wait(NULL);
    }

    return 0;
}
