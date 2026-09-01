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
                    return;
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

// pipe에 남은 stdout/stderr를 읽어서 out_buffer/stderror에 채우는 함수.
// SIGCHLD 핸들러에서 malloc/realloc을 직접 호출하면 async-signal-unsafe라
// 여기로 분리함 - select 루프에서 해당 job의 readpipe/errpipe가 readable할
// 때 호출하면 됨.
void read_job_output(int job_id, int is_stderr) {
    char buf[1024];
    int buf_count = 0;
    int max_length = 1024;
    int pipe_fd = is_stderr ? jobs[job_id].errpipe : jobs[job_id].readpipe;
    char **out = is_stderr ? &jobs[job_id].stderror : &jobs[job_id].out_buffer;

    *out = (char *)malloc((max_length + 1) * sizeof(char));

    while (1) {
        int len = read(pipe_fd, buf, sizeof(buf));
        if (len <= 0)
            break;
        buf_count += len;
        if (max_length < buf_count) {
            *out = realloc(*out, (buf_count + 1) * 2 * sizeof(char));
            max_length = (buf_count + 1) * 2;
        }

        memcpy(*out + (buf_count - len), buf, len);
    }
    (*out)[buf_count] = 0;

    close(pipe_fd);
}

// jobs 슬롯 초기화. main 시작할 때 한 번 호출.
void init_jobs(void) {
    for (int i = 0; i < 10; i++) {
        jobs[i].valid_vit = 0;
        jobs[i].readpipe = -1;
        jobs[i].errpipe = -1;
        jobs[i].pid = -1;
        jobs[i].status = "EMPTY";
        jobs[i].out_buffer = NULL;
        jobs[i].stderror = NULL;
    }
}

// SIGCHLD 핸들러 등록. main 시작할 때 한 번 호출.
void install_sigchld_handler(void) { signal(SIGCHLD, sigchld_handler); }

void printjob(int i, int fd) {
    dprintf(fd, "JOB %d %s %s\n", i, jobs[i].status, jobs[i].cmd);
}

void read_command(const char *input_arr, int fd) {
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
                printjob(i, fd);
            }
        }
        dprintf(fd, "END\n");
        return;
    }
    if (!strcmp("RESULT", command)) { // 커맨드
        dprintf(fd, "id : %d\n", id);
        if (id < 0 || id >= 10 || jobs[id].valid_vit == 0) {
            dprintf(fd, "작업 존재하지 않음");
            return;
        }
        if (!strcmp("DONE", jobs[id].status)) {
            dprintf(fd,
                    "DONE exitcode: %d output length: %d error length: %d\n",
                    jobs[id].exitcode, (int)strlen(jobs[id].out_buffer),
                    (int)strlen(jobs[id].stderror));
            dprintf(fd, "stdout : \n%s\nstderr :\n%s\n", jobs[id].out_buffer,
                    jobs[id].stderror);
        } else if (!strcmp("RUNNING", jobs[id].status)) {

            dprintf(fd, "RUNING\n");
        } else if (!strcmp("KILLED", jobs[id].status)) {
            dprintf(fd, "KILLED\n");
        } else if (!strcmp("ERROR", jobs[id].status)) {
            dprintf(fd,
                    "ERROR exitcode: %d output length: %d error length: %d\n",
                    jobs[id].exitcode, (int)strlen(jobs[id].out_buffer),
                    (int)strlen(jobs[id].stderror));
            dprintf(fd, "stdout : \n%s\nstderr :\n%s\n", jobs[id].out_buffer,
                    jobs[id].stderror);

        } else if (jobs[id].valid_vit == 0) {

            dprintf(fd, "ERR no such job");
        }
        return;
    }
    if (!strcmp("KILL", command)) { // 커맨드
        if (id < 0 || id >= 10 || jobs[id].valid_vit == 0) {
            dprintf(fd, "ERR no such job\n");
            return;
        }
        if (!strcmp(jobs[id].status, "DONE")) {
            dprintf(fd, "ERR job already finished\n");
            return;
        }
        kill(jobs[id].pid, SIGTERM);
        return;
    }
    if (!strcmp("RUN", command)) { // 커맨드
        fork_execute(argv, fd);
        return;
    }
}
void fork_execute(char *argv[], int fd) {
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
        dprintf(fd, "RUN error\n");
        jobs[job_id].status = "ERROR";
        jobs[job_id].error_bit = 1;
    }
    free(argv);
    return;
}
