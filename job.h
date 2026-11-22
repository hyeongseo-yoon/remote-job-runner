#ifndef JOB_H
#define JOB_H

#include <signal.h>
#include <stddef.h>
#include <sys/types.h>

#define MAX_JOBS 32
#define MAX_ARGS 64
#define MAX_OUTPUT (1 << 20) // job 하나당 stdout/stderr 각각 최대 저장량 (초과분은 버림)

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} outbuf;

// job 상태 저장 전역 구조체
typedef struct {
    pid_t pid;
    int valid_vit; // 1이면 사용 중인 슬롯
    unsigned long seq; // 생성 순서. 슬롯 부족 시 가장 오래된 종료 job부터 회수
    char *cmd;     // LIST에 보여줄 명령줄 전체
    int readpipe;  // 자식 stdout을 읽는 fd, EOF 후 -1
    int errpipe;   // 자식 stderr를 읽는 fd, EOF 후 -1
    outbuf out;
    outbuf err;
    int exec_errno; // execvp 실패 시 errno, 성공이면 0
    volatile sig_atomic_t exited; // SIGCHLD 핸들러가 세팅
    volatile int wstatus;         // 핸들러가 기록한 waitpid status
} job;

extern job jobs[MAX_JOBS];

// main 시작할 때 한 번씩 호출
void init_jobs(void);
void install_sigchld_handler(void);

// 핸들러는 waitpid로 종료 상태만 기록 (async-signal-safe한 것만 사용)
void sigchld_handler(int sig);

// job의 stdout/stderr 파이프에서 한 번 읽어 버퍼에 쌓음. select에서 해당 fd가 readable일 때 호출.
// EOF를 만나면 fd를 닫고 -1로 바꿈.
void read_job_output(int job_id, int is_stderr);

// "RUN ls -la" 같은 한 줄(개행 제외)을 해석해서 실행하고 응답은 fd(클라이언트 소켓)로 보냄
void read_command(const char *line, int fd);

#endif
