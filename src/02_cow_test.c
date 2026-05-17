#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main() {
    int num = 100;
    pid_t pid = fork();
    if (pid == 0) {
        printf("child before write: %d\n", num);
        num = 42; // Trigger COW
        printf("child after write: %d\n", num);
    } else if (pid > 0) {
        wait(NULL);
        printf("parent value: %d\n", num);
    }
    return 0;
}
