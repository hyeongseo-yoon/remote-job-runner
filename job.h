#ifndef JOB_H
#define JOB_H
// #define _GNU_SOURCE
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

void sigchld_handler(int sig);
void printjob(int i);
void read_command(
    char *input_arr); // 입력 문자열 받아서 명령어 해석. - 199줄 까지의 로직
void fork_execute();  // 199줄 이후 로직

#endif
