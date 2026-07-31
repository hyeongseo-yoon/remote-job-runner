#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// job 상태 저장

typedef struct {
    pid_t pid;
    char *status;
    char *cmd;
    int exitcode;
    int readpipe;
    int errpipe;
    char *out_buffer;
    char *stderror;
    int valid_vit;
} job;

job jobs[10];

int pipefd[2] = {0};
int err_fd[2] = {0};
int wstatus;

void sigchld_handler(int sig) {
    volatile int pid = 1;
    while (1) {
        pid = waitpid(-1, &wstatus, WNOHANG);
        if (pid == 0)
            return;
        char buf[1024];
        for (int i = 0; i < 10; i++) {
            if (pid == jobs[i].pid) {
                //
                if (WIFEXITED(wstatus)) {
                    // 상태 기록
                    jobs[i].status = "TERMINATED";
                    jobs[i].exitcode = WEXITSTATUS(wstatus);
                    int buf_count = 0;
                    jobs[i].out_buffer =
                        (char *)malloc((buf_count + 1) * 1024 * sizeof(char));

                    int j = 0;
                    // out_buffer 저장
                    while (read(jobs[i].readpipe, buf, sizeof(buf))) {
                        if (buf_count < j) {
                            jobs[i].out_buffer = realloc(
                                jobs[i].out_buffer, (++buf_count + 1) * 1024);
                        }
                        memcpy(jobs[i].out_buffer + buf_count * 1024, buf,
                               1024);
                        j++;
                    }
                    // stderror 저장
                    j = 0;
                    buf_count = 0;
                    jobs[i].stderror =
                        (char *)malloc((buf_count + 1) * 1024 * sizeof(char));

                    while (read(jobs[i].errpipe, buf, sizeof(buf))) {
                        if (buf_count < j) {
                            jobs[i].stderror = realloc(
                                jobs[i].stderror, (++buf_count + 1) * 1024);
                        }
                        memcpy(jobs[i].stderror + buf_count * 1024, buf, 1024);
                        j++;
                    }

                    printf("%s\n", jobs[i].out_buffer);
                    printf("exitcode : %d\n", jobs[i].exitcode);
                    return;
                    break;
                }
            }
        }
        // 여기서 부터는 에러
    }
}

int main() {
    signal(SIGCHLD, sigchld_handler);
    while (1) {

        char input_arr[1024];
        int input_count = 0;
        char **argv = (char **)malloc(10 * sizeof(char *));
        int argv_len = 10;
        scanf("%s", input_arr);

        if (!strcmp("LIST", input_arr)) { // 커맨드
            printf("LIST\n");
            continue;
        }
        if (!strcmp("RESULT", input_arr)) { // 커맨드
            printf("RESULT\n");
            continue;
        }
        if (!strcmp("KILL", input_arr)) { // 커맨드
            printf("KILL\n");
            continue;
        }
        if (strcmp("RUN", input_arr)) { // 커맨드
            continue;
        }

        while (scanf("%s", input_arr) != EOF) {
            if (argv_len >= input_count) { // 재할당
                argv = realloc(argv, argv_len * 2);
                argv_len *= 2;
            }
            argv[input_count] =
                (char *)malloc(strlen(input_arr) * sizeof(char));
            strcpy(argv[input_count], input_arr);

            input_count++;

            if (getchar() == '\n') {
                break;
            }
        }

        pipe(pipefd);
        pipe2(err_fd, O_CLOEXEC);
        pid_t pid = fork();
        if (pid < 0) {

            return 1;
        }
        // 자식
        if (pid == 0) {
            close(pipefd[0]);
            close(err_fd[0]);
            dup2(pipefd[1], 1);
            close(pipefd[1]);
            execvp(argv[0], argv);

            int e = errno;
            write(err_fd[1], &e, sizeof(e));
            _exit(127);
        }

        // 부모
        close(pipefd[1]);
        close(err_fd[1]);
        int e[1] = {0};
        for (int i = 0; i < 10; i++) {
            if (jobs[i].valid_vit == 0) {
                jobs[i].pid = pid;
                jobs[i].status = "RUNNING";
                jobs[i].cmd = argv[0];
                jobs[i].valid_vit = 1;
                jobs[i].readpipe = pipefd[0];
                jobs[i].errpipe = err_fd[0];

                break;
            }
        }
        read(err_fd[0], e, sizeof(e));

        if (e[0] == 0) {
            // 성공 -> pid 기록하고 keep going
            free(argv);

            printf("continue\n");
            continue;

        } else {
            // 실패
            printf("error\n");
        }
        free(argv);

        return 0;
    }
}
