#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>

/* pagemap entry: 64 bits per page */
/* bits 0-54: PFN if present       */
/* bit 63:   page present           */
static uint64_t get_pfn(void *vaddr) {
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) { perror("open pagemap"); return 0; }

    uint64_t vpn = (uint64_t)vaddr / 4096;      /* virtual page number */
    uint64_t offset = vpn * 8;                    /* 8 bytes per entry */
    uint64_t entry = 0;

    if (pread(fd, &entry, 8, offset) != 8) {
        perror("pread pagemap");
        close(fd);
        return 0;
    }
    close(fd);

    if (!(entry & (1ULL << 63))) {                /* bit 63 = present */
        return 0;                                 /* not resident */
    }
    return entry & ((1ULL << 55) - 1);            /* bits 0-54 = PFN */
}

int main() {
    int num = 100;

    /* touch num so page is faulted in */
    volatile int sink = num;
    (void)sink;

    pid_t pid = fork();

    if (pid == 0) {
        /* child reads PFN before write */
        uint64_t pfn_before = get_pfn(&num);
        printf("child  pid=%d  PFN_BEFORE_WRITE = 0x%lx  num=%d\n",
               getpid(), pfn_before, num);

        /* write triggers COW */
        num = 42;

        /* child reads PFN after write */
        uint64_t pfn_after = get_pfn(&num);
        printf("child  pid=%d  PFN_AFTER_WRITE  = 0x%lx  num=%d\n",
               getpid(), pfn_after, num);

        if (pfn_before == pfn_after)
            printf("SAME frame (no COW yet?)\n");
        else
            printf("DIFFERENT frame -> COW allocated new page\n");

    } else if (pid > 0) {
        /* parent reads PFN immediately */
        uint64_t pfn_parent = get_pfn(&num);
        printf("parent pid=%d  PFN              = 0x%lx  num=%d\n",
               getpid(), pfn_parent, num);
        wait(NULL);
    }
    return 0;
}
