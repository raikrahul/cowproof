#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

/* read PFN from /proc/<pid>/pagemap for virtual address vaddr */
static uint64_t get_pfn_of(pid_t target, void *vaddr) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/pagemap", target);
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return 0; }
    uint64_t vpn = (uint64_t)vaddr / 4096;
    uint64_t entry = 0;
    if (pread(fd, &entry, 8, vpn * 8) != 8) {
        perror("pread");
        close(fd);
        return 0;
    }
    close(fd);
    if (!(entry & (1ULL << 63))) return 0;
    return entry & ((1ULL << 55) - 1);
}

int main() {
    /* STEP 1: mmap one page — num lives here, isolated from stack */
    int *num = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    *num = 100;

    /* pipe for sync: child blocks until parent finishes reading */
    int pipefd[2];
    pipe(pipefd);

    pid_t child = fork();

    if (child == 0) {
        close(pipefd[1]);
        /* STEP 2: child does NOTHING — just waits */
        char c;
        read(pipefd[0], &c, 1);  /* blocks here */
        close(pipefd[0]);

        /* STEP 4: now child writes — COW should fire here */
        *num = 42;

        /* signal parent again */
        kill(getppid(), SIGUSR1);
        /* hold still so parent can read post-write PFN */
        sleep(1);
        _exit(0);

    } else {
        close(pipefd[0]);
        usleep(50000); /* let child reach the read() block */

        /* STEP 3: parent reads BOTH pagemaps — child hasn't touched anything */
        uint64_t pfn_parent = get_pfn_of(getpid(), num);
        uint64_t pfn_child  = get_pfn_of(child, num);

        printf("=== BEFORE ANY WRITE ===\n");
        printf("parent PFN = 0x%lx   (pid %d)\n", pfn_parent, getpid());
        printf("child  PFN = 0x%lx   (pid %d)\n", pfn_child, child);
        if (pfn_parent == pfn_child)
            printf("SAME physical frame: 0x%lx * 4096 = phys 0x%lx\n",
                   pfn_parent, pfn_parent * 4096);
        else
            printf("DIFFERENT frames\n");

        /* unblock child — let it write *num = 42 */
        write(pipefd[1], "g", 1);
        close(pipefd[1]);

        /* wait for child's signal that write is done */
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGUSR1);
        sigprocmask(SIG_BLOCK, &set, NULL);
        int sig;
        sigwait(&set, &sig);

        /* read both pagemaps again */
        uint64_t pfn_parent2 = get_pfn_of(getpid(), num);
        uint64_t pfn_child2  = get_pfn_of(child, num);

        printf("\n=== AFTER CHILD WRITES num=42 ===\n");
        printf("parent PFN = 0x%lx   num=%d\n", pfn_parent2, *num);
        printf("child  PFN = 0x%lx\n", pfn_child2);
        if (pfn_parent2 != pfn_child2)
            printf("DIFFERENT frames -> COW split confirmed\n");
        else
            printf("SAME frame still\n");

        wait(NULL);
        munmap(num, 4096);
    }
    return 0;
}
