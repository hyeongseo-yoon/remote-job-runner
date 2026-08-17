#include "job.h"

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

void printjob(int i);
void read_command(char *input_arr) {
    // arr = "RUN ls -la\n" 형태 - 하나씩 파싱해서 실행하면 될듯
}
void fork_execute();
