#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

int main() {
    int *num = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    // Actually, to test COW it MUST be MAP_PRIVATE!
    munmap(num, 4096);
    num = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    *num = 100;

    int pipe_p2c[2], pipe_c2p[2];
    pipe(pipe_p2c); pipe(pipe_c2p);

    pid_t pid = fork();
    if (pid == 0) {
        char c;
        // Step 0: Initial state
        read(pipe_p2c[0], &c, 1);
        
        // Step 3: Wait for parent to write 7
        read(pipe_p2c[0], &c, 1);
        
        // Step 4: Child writes 42
        *num = 42;
        write(pipe_c2p[1], "x", 1);
        
        exit(0);
    } else {
        char c;
        printf("Parent PID=%d, Child PID=%d, VA=%p\n", getpid(), pid, num);
        printf("--- STEP 0: After fork, before writes ---\n");
        printf("Run: cat /proc/refproof (set target_pid and target_va)\n");
        getchar();
        write(pipe_p2c[1], "x", 1);
        
        printf("--- STEP 1 & 2: Parent writes num=7 ---\n");
        *num = 7;
        printf("Run driver again. Parent got new frame, child kept old.\n");
        getchar();
        write(pipe_p2c[1], "x", 1);
        
        read(pipe_c2p[0], &c, 1);
        printf("--- STEP 4: Child writes num=42 (refcount was 1, flips in place) ---\n");
        printf("Run driver again. Child should have R/W=1, SAME old frame.\n");
        getchar();
    }
    return 0;
}
