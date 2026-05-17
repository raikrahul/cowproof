#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>

/* pagemap: 8 bytes per page, bits 0-54 = PFN, bit 63 = present */
static uint64_t get_pfn(void *vaddr) {
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) { perror("open pagemap"); return 0; }
    uint64_t vpn = (uint64_t)vaddr / 4096;
    uint64_t entry = 0;
    if (pread(fd, &entry, 8, vpn * 8) != 8) {
        perror("pread pagemap");
        close(fd);
        return 0;
    }
    close(fd);
    if (!(entry & (1ULL << 63))) return 0;
    return entry & ((1ULL << 55) - 1);
}

int main() {
    int pipefd[2];
    pipe(pipefd);

    /* force num's page resident before fork */
    volatile int num = 100;
    volatile int sink = num;
    (void)sink;

    /* read PFN in parent BEFORE fork */
    uint64_t pfn_parent_pre = get_pfn((void*)&num);

    pid_t pid = fork();

    if (pid == 0) {
        close(pipefd[0]);

        /* child reads PFN immediately — no writes yet */
        uint64_t pfn_child_before = get_pfn((void*)&num);

        /* child writes num = 42 (triggers COW) */
        num = 42;

        /* child reads PFN after write */
        uint64_t pfn_child_after = get_pfn((void*)&num);

        /* send all three PFNs through pipe (no printf = no COW on stdio) */
        uint64_t buf[3] = { pfn_child_before, pfn_child_after, (uint64_t)num };
        write(pipefd[1], buf, sizeof(buf));
        close(pipefd[1]);
        _exit(0);

    } else {
        close(pipefd[1]);

        /* parent reads PFN after fork */
        uint64_t pfn_parent_post = get_pfn((void*)&num);

        /* wait for child data */
        uint64_t buf[3];
        read(pipefd[0], buf, sizeof(buf));
        close(pipefd[0]);
        wait(NULL);

        uint64_t pfn_child_before = buf[0];
        uint64_t pfn_child_after  = buf[1];
        int child_num             = (int)buf[2];

        printf("=== PFN PROOF ===\n");
        printf("parent PFN (pre-fork):     0x%lx\n", pfn_parent_pre);
        printf("parent PFN (post-fork):    0x%lx\n", pfn_parent_post);
        printf("child  PFN (before write): 0x%lx\n", pfn_child_before);
        printf("child  PFN (after write):  0x%lx\n", pfn_child_after);
        printf("\n");

        if (pfn_parent_post == pfn_child_before)
            printf("BEFORE WRITE: parent PFN == child PFN -> SAME physical frame\n");
        else
            printf("BEFORE WRITE: parent PFN != child PFN -> ALREADY SPLIT\n");

        if (pfn_child_before != pfn_child_after)
            printf("AFTER WRITE:  child PFN changed 0x%lx -> 0x%lx -> COW fired\n",
                   pfn_child_before, pfn_child_after);
        else
            printf("AFTER WRITE:  child PFN unchanged (stack page was already private)\n");

        printf("\nparent num=%d  child num=%d\n", num, child_num);
    }
    return 0;
}
