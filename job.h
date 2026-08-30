#ifndef JOB_H
#define JOB_H
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
// job 상태 저장 전역 구조체
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

extern job jobs[10];

extern int pipefd[2];
extern int err_fd[2];
extern int wstatus;
extern char **argv;            // execvp에 들어갈 argv 문자열 배열
void sigchld_handler(int sig); // sigchld 핸들러. 좀비 리핑 + 상태값만 기록 (async-signal-safe하게 최소한만)
void read_job_output(
    int job_id, int is_stderr); // job_id의 readpipe/errpipe를 읽어서 out_buffer/stderror 채움.
                 // select 루프에서 해당 fd가 readable할 때 호출.
                 // 여기서 stderr가 없을 수도 있는데 그냥 다 읽으면 문제가 생길 수 도 있을 거 같아서 나누는게 좋을듯
void printjob(int i, int fd);
void read_command(const char *input_arr,
                   int fd); // 입력 문자열 받아서 명령어 해석/실행 함수.
                            // input_arr는 클라이언트가 보낸 메시지 그대로 가져옴 ex) input_arr = "RUN ls -la\n"
                            // fd는 결과를 돌려줄 클라이언트 소켓
void fork_execute(
    char *argv[],
    int fd); // 199줄 이후 로직 read_command 안에서 호출하는 함수임. fd는 exec
             // 실패 시 에러를 돌려줄 클라이언트 소켓


// 둘 다 main 시작하고 필요한 함수들
void init_jobs(void);
/*
valid_vit = 0
readpipe = -1
errpipe = -1
pid = -1
status = "EMPTY"
out_buffer = NULL
stderror = NULL
방식으로 초기화하는게 나을듯
*/

//이건 기존 handler 등록할 수 있게 만들어주면 될듯
void install_sigchld_handler(void);
    
#endif