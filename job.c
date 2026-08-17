#include "job.h"
#include <stdlib.h>
#include <string.h>

job jobs[10];

int pipefd[2] = {0};
int err_fd[2] = {0};
int wstatus;
char **argv;
int input_count = 0;
int argv_len = 10;
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

void read_command(const char *input_arr) {
    // arr = "RUN ls -la\n" 형태 - 커맨드 / 토큰 단위로 나눠서 생각하면 됨.
    // 명령어 단위로 끊어서 입력을 받은 다음 비교하는 방향으로 바꿈.
    input_count = 0;
    argv = (char **)malloc(10 * sizeof(char *));
    argv_len = 10;

    char command[256] = {0}; // input_arr 쪼갠 단위. 명령어 파싱
    size_t len = strcspn(input_arr, " \n");
    memcpy(command, input_arr, len);
    command[len] = 0;

    const char *p = input_arr + len;
    char temp[32] = {0};
    int id = 0;
    if (!strcmp("RESULT", command) || !strcmp("KILL", command)) {
        while (*p) { // job_id 파싱 (주어진 경우);
            while (*p == ' ')
                p++;
            if (*p == '\n' || *p == '\0')
                break;

            len = strcspn(p, " \n");
            memcpy(temp, p, len);
            temp[len] = 0;
            id = atoi(temp);
            break;
        }
    }

    int i = 0;
    argv = malloc(sizeof(char *) * argv_len);

    while (*p) { // 토큰 하나씩 파싱 + argv에 하나씩 넣음.
        while (*p == ' ')
            p++;
        if (*p == '\n' || *p == '\0')
            break;

        len = strcspn(p, " \n");
        char token[256] = {0};
        memcpy(token, p, len);
        token[len] = 0;
        if (i >= argv_len) {
            argv = realloc(argv, argv_len * 2 * sizeof(char *));
            argv_len *= 2;
        }
        argv[i] = malloc(sizeof(char) * 256);
        strcpy(argv[i], token);
        p += len;
        i++;
    }
    if (i >= argv_len) {
        argv = realloc(argv, argv_len * 2 * sizeof(char *));
        argv_len *= 2;
    }
    argv[i] = NULL;

    if (!strcmp("LIST", command)) { // 커맨드
        for (int i = 0; i < 10; i++) {
            if (jobs[i].valid_vit == 1) {
                printjob(i);
            }
        }
        printf("END\n");
        return;
    }
    if (!strcmp("RESULT", command)) { // 커맨드
        printf("id : %d\n", id);
        if (jobs[id].valid_vit == 0) {
            printf("작업 존재하지 않음");
            return;
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
            printf("ERROR exitcode: %d output length: %d error length: %d\n",
                   jobs[id].exitcode, (int)strlen(jobs[id].out_buffer),
                   (int)strlen(jobs[id].stderror));
            printf("stdout : \n%s\nstderr :\n%s\n", jobs[id].out_buffer,
                   jobs[id].stderror);

        } else if (jobs[id].valid_vit == 0) {

            printf("ERR no such job");
        }
        return;
    }
    if (!strcmp("KILL", command)) { // 커맨드
        if (!strcmp(jobs[id].status, "DONE")) {
            printf("ERR job already finished\n");
            return;
        }
        kill(jobs[id].pid, SIGTERM);
        return;
    }
    if (!strcmp("RUN", command)) { // 커맨드
        fork_execute(argv);
        return;
    }
}
void fork_execute(char *argv[]) {
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

        return;
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

    } else {
        // 실패
        printf("RUN error\n");
        jobs[job_id].status = "ERROR";
        jobs[job_id].error_bit = 1;
    }
    free(argv);
    return;
}
