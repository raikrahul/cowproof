#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main() {
    int num = 100;
    pid_t pid = fork();
    if (pid == 0) {
        printf("value in child process: %d\n", num);
    } else if (pid > 0) {
        printf("value in parent process: %d\n", num);
        wait(NULL);
    } else {
        perror("fork");
        return 1;
    }
    return 0;
}
