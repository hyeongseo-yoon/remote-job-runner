#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
// job 상태 저장
struct job {
    pid_t pid;
    char *status;
    char *cmd;
} typedef job;

int main() {
    int pipe(int pipefd[2]);
    int err_pipe(int err_fd[2]);

    pid_t pid = fork();
    if (pid < 0) {
    }
    // 자식
    if (pid == 0) {
    }
    // 부모
}
