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
    int error_bit;
} job;

job jobs[10];

int pipefd[2] = {0};
int err_fd[2] = {0};
int wstatus;

void sigchld_handler(int sig) {
    volatile int pid = 1;
    while (1) {
        pid = waitpid(-1, &wstatus, WNOHANG);
        if (pid <= 0)
            return;
        char buf[1024];
        for (int i = 0; i < 10; i++) {
            if (pid == jobs[i].pid) {
                //
                if (WIFEXITED(wstatus)) {
                    // 상태 기록
                    if (jobs[i].error_bit == 1) {
                        jobs[i].status = "ERROR";
                    } else {
                        jobs[i].status = "DONE";
                    }
                    jobs[i].exitcode = WEXITSTATUS(wstatus);
                    int buf_count = 0;
                    int max_length = 1024;
                    jobs[i].out_buffer =
                        (char *)malloc((max_length + 1) * sizeof(char));
                    jobs[i].stderror =
                        (char *)malloc((max_length + 1) * sizeof(char));

                    // out_buffer 저장
                    while (1) {
                        int len = read(jobs[i].readpipe, buf, sizeof(buf));
                        if (len <= 0)
                            break;
                        buf_count += len;
                        if (max_length < buf_count) {
                            jobs[i].out_buffer =
                                realloc(jobs[i].out_buffer,
                                        (buf_count + 1) * 2 * sizeof(char));
                            max_length = (buf_count + 1) * 2;
                        }

                        memcpy(jobs[i].out_buffer + (buf_count - len), buf,
                               len);
                    }

                    jobs[i].out_buffer[buf_count] = 0;
                    // stderror 저장
                    buf_count = 0;
                    max_length = 1024;
                    while (1) {
                        int len = read(jobs[i].errpipe, buf, sizeof(buf));
                        if (len <= 0)
                            break;
                        buf_count += len;
                        if (max_length < buf_count) {
                            jobs[i].stderror =
                                realloc(jobs[i].stderror,
                                        (buf_count + 1) * 2 * sizeof(char));
                            max_length = (buf_count + 1) * 2;
                        }

                        memcpy(jobs[i].stderror + (buf_count - len), buf, len);
                    }
                    jobs[i].stderror[buf_count] = 0;

                    printf("%s\n", jobs[i].out_buffer);
                    printf("exitcode: %d\n", jobs[i].exitcode);
                    close(jobs[i].readpipe);
                    close(jobs[i].errpipe);
                    return;
                    break;
                } else if (WIFSIGNALED(wstatus)) {
                    jobs[i].status = "KILLED";
                    return;
                }
            }
        }
        // 여기서 부터는 에러
        printf("error\n");
        return;
    }
}

void printjob(int i) {
    printf("JOB %d %s %s\n", i, jobs[i].status, jobs[i].cmd);
}
int main() {
    signal(SIGCHLD, sigchld_handler);
    while (1) {

        char input_arr[1024];
        int input_count = 0;
        char **argv = (char **)malloc(10 * sizeof(char *));
        int argv_len = 10;
        if (scanf("%s", input_arr) == EOF)
            break;

        if (!strcmp("LIST", input_arr)) { // 커맨드
            for (int i = 0; i < 10; i++) {
                if (jobs[i].valid_vit == 1) {
                    printjob(i);
                }
            }
            printf("END\n");
            continue;
        }
        if (!strcmp("RESULT", input_arr)) { // 커맨드
            scanf("%s", input_arr);
            int id = atoi(input_arr);
            printf("id : %d\n", id);
            if (jobs[id].valid_vit == 0) {
                printf("작업 존재하지 않음");
                continue;
            }
            if (!strcmp("DONE", jobs[id].status)) {
                printf("DONE exitcode: %d output length: %d error length: %d\n",
                       jobs[id].exitcode, (int)strlen(jobs[id].out_buffer),
                       (int)strlen(jobs[id].stderror));
                printf("stdout : \n%s\nstderr :\n%s\n", jobs[id].out_buffer,
                       jobs[id].stderror);
            } else if (!strcmp("RUNNING", jobs[id].status)) {

                printf("RUNING\n");
            } else if (!strcmp("KILLED", jobs[id].status)) {
                printf("KILLED\n");
            } else if (!strcmp("ERROR", jobs[id].status)) {
                printf(
                    "ERROR exitcode: %d output length: %d error length: %d\n",
                    jobs[id].exitcode, (int)strlen(jobs[id].out_buffer),
                    (int)strlen(jobs[id].stderror));
                printf("stdout : \n%s\nstderr :\n%s\n", jobs[id].out_buffer,
                       jobs[id].stderror);

            } else if (jobs[id].valid_vit == 0) {

                printf("ERR no such job");
            }

            continue;
        }
        if (!strcmp("KILL", input_arr)) { // 커맨드
            scanf("%s", input_arr);
            int id = atoi(input_arr);
            if (!strcmp(jobs[id].status, "DONE")) {
                printf("ERR job already finished\n");
                continue;
            }
            kill(jobs[id].pid, SIGTERM);
            continue;
        }
        if (strcmp("RUN", input_arr)) { // 커맨드
            continue;
        }

        while (scanf("%s", input_arr) != EOF) {
            if (input_count >= argv_len) { // 재할당
                argv = realloc(argv, argv_len * 2 * sizeof(char *));
                argv_len *= 2;
            }
            argv[input_count] =
                (char *)malloc((strlen(input_arr) + 1) * sizeof(char));
            strcpy(argv[input_count], input_arr);

            input_count++;

            if (getchar() == '\n') {
                break;
            }
        }

        pipe(pipefd);
        pipe2(err_fd, O_CLOEXEC);
        pid_t pid = fork();
        int job_id;
        for (int i = 0; i < 10; i++) {
            if (jobs[i].valid_vit == 0) {
                jobs[i].pid = pid;
                jobs[i].status = "PENDING";
                jobs[i].cmd = argv[0];
                jobs[i].valid_vit = 1;
                jobs[i].readpipe = pipefd[0];
                jobs[i].errpipe = err_fd[0];
                job_id = i;
                break;
            }
        }

        if (pid < 0) {

            return 1;
        }
        // 자식
        if (pid == 0) {
            argv[input_count] = 0;
            close(pipefd[0]);
            close(err_fd[0]);
            dup2(pipefd[1], 1);
            close(pipefd[1]);

            execvp(argv[0], argv);

            int e = errno;
            write(err_fd[1], &e, sizeof(e));
            close(err_fd[1]);
            _exit(127);
        }

        // 부모
        close(pipefd[1]);
        close(err_fd[1]);
        int e[1] = {0};
        read(err_fd[0], e, sizeof(e));

        if (e[0] == 0) {
            // 성공 -> pid 기록하고 keep going
            free(argv);

            jobs[job_id].status = "RUNNING";
            continue;

        } else {
            // 실패
            printf("RUN error\n");
            jobs[job_id].status = "ERROR";
            jobs[job_id].error_bit = 1;
            continue;
        }
        free(argv);

        return 0;
    }
}
