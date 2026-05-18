#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>

int main() {
    int *num = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    *num = 100;
    
    FILE *f = fopen("/tmp/cow_addrs", "w");
    fprintf(f, "%p\n", num);
    fclose(f);

    pid_t pid = fork();
    if (pid == 0) {
        FILE *f2 = fopen("/tmp/cow_child_pid", "w");
        fprintf(f2, "%d\n", getpid());
        fclose(f2);
        sleep(10); // Wait for driver to inspect
        exit(0);
    } else {
        FILE *f3 = fopen("/tmp/cow_parent_pid", "w");
        fprintf(f3, "%d\n", getpid());
        fclose(f3);
        sleep(10); // Wait for driver to inspect
    }
    return 0;
}
