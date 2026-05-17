#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>

/* child signals parent when ready to be walked */
volatile sig_atomic_t got_signal = 0;
void handler(int s) { got_signal = 1; }

int main() {
    int *num = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    *num = 100;

    signal(SIGUSR1, handler);

    printf("parent pid=%d  &num=%p\n", getpid(), (void*)num);
    fflush(stdout);

    pid_t child = fork();

    if (child == 0) {
        printf("child  pid=%d  &num=%p  (same VA, shared PFN right now)\n",
               getpid(), (void*)num);
        fflush(stdout);

        /* signal parent, then sleep so ptwalk can read us */
        kill(getppid(), SIGUSR1);
        sleep(30);  /* BEFORE write — ptwalk reads here */
        _exit(0);
    } else {
        /* wait for child to be ready */
        while (!got_signal) usleep(1000);

        printf("\nBoth processes sleeping. Run:\n");
        printf("  sudo insmod ptwalk.ko target_pid=%d target_va=%lu\n",
               getpid(), (unsigned long)num);
        printf("  sudo cat /proc/ptwalk   # parent walk\n");
        printf("  sudo rmmod ptwalk\n");
        printf("  sudo insmod ptwalk.ko target_pid=%d target_va=%lu\n",
               child, (unsigned long)num);
        printf("  sudo cat /proc/ptwalk   # child walk\n");
        printf("  sudo rmmod ptwalk\n");
        fflush(stdout);

        /* hold still for 30 seconds */
        sleep(30);
        wait(NULL);
    }
    return 0;
}
