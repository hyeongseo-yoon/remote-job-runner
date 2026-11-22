#define _GNU_SOURCE
#include "job.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

job jobs[MAX_JOBS];
static unsigned long next_seq = 1;

// SIGCHLD 핸들러. 좀비 리핑 + 종료 상태 기록만 함 (malloc/printf 금지)
void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno;
    int ws;
    pid_t pid;
    while ((pid = waitpid(-1, &ws, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jobs[i].valid_vit && jobs[i].pid == pid) {
                jobs[i].wstatus = ws;
                jobs[i].exited = 1;
                break;
            }
        }
    }
    errno = saved_errno;
}

void install_sigchld_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
}

void init_jobs(void) {
    memset(jobs, 0, sizeof(jobs));
    for (int i = 0; i < MAX_JOBS; i++) {
        jobs[i].readpipe = -1;
        jobs[i].errpipe = -1;
        jobs[i].pid = -1;
    }
}

static void outbuf_append(outbuf *b, const char *data, size_t n) {
    if (b->len >= MAX_OUTPUT)
        return;
    if (n > MAX_OUTPUT - b->len)
        n = MAX_OUTPUT - b->len;
    if (b->len + n > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 1024;
        while (ncap < b->len + n)
            ncap *= 2;
        char *p = realloc(b->data, ncap);
        if (!p)
            return;
        b->data = p;
        b->cap = ncap;
    }
    memcpy(b->data + b->len, data, n);
    b->len += n;
}

void read_job_output(int job_id, int is_stderr) {
    job *j = &jobs[job_id];
    int *fdp = is_stderr ? &j->errpipe : &j->readpipe;
    outbuf *b = is_stderr ? &j->err : &j->out;
    if (*fdp < 0)
        return;

    char buf[4096];
    ssize_t n = read(*fdp, buf, sizeof(buf));
    if (n < 0) {
        if (errno == EAGAIN || errno == EINTR)
            return;
        n = 0;
    }
    if (n == 0) {
        close(*fdp);
        *fdp = -1;
        return;
    }
    outbuf_append(b, buf, (size_t)n);
}

// 종료 + 출력 수집까지 끝나야 최종 상태로 인정
static const char *job_status(int i) {
    job *j = &jobs[i];
    if (!j->valid_vit)
        return "EMPTY";
    if (!j->exited || j->readpipe >= 0 || j->errpipe >= 0)
        return "RUNNING";
    if (j->exec_errno)
        return "ERROR";
    if (WIFSIGNALED(j->wstatus))
        return "KILLED";
    return "DONE";
}

static int job_finished(int i) {
    const char *s = job_status(i);
    return strcmp(s, "RUNNING") && strcmp(s, "EMPTY");
}

static void free_job(int i) {
    job *j = &jobs[i];
    free(j->cmd);
    free(j->out.data);
    free(j->err.data);
    memset(j, 0, sizeof(*j));
    j->readpipe = -1;
    j->errpipe = -1;
    j->pid = -1;
}

// 빈 슬롯을 찾고, 없으면 끝난 job 하나를 회수해서 재사용
static int alloc_slot(void) {
    for (int i = 0; i < MAX_JOBS; i++)
        if (!jobs[i].valid_vit)
            return i;
    int oldest = -1;
    for (int i = 0; i < MAX_JOBS; i++)
        if (job_finished(i) && (oldest < 0 || jobs[i].seq < jobs[oldest].seq))
            oldest = i;
    if (oldest >= 0)
        free_job(oldest);
    return oldest;
}

static int write_all(int fd, const char *data, size_t len) {
    while (len > 0) {
        ssize_t n = send(fd, data, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

static void fork_execute(char *argv[], const char *cmdline, int fd) {
    int slot = alloc_slot();
    if (slot < 0) {
        dprintf(fd, "ERR too many jobs\n");
        return;
    }

    int op[2], ep[2], xp[2]; // stdout, stderr, exec 실패 errno 파이프
    if (pipe2(op, O_CLOEXEC) < 0)
        goto fail;
    if (pipe2(ep, O_CLOEXEC) < 0) {
        close(op[0]);
        close(op[1]);
        goto fail;
    }
    if (pipe2(xp, O_CLOEXEC) < 0) {
        close(op[0]);
        close(op[1]);
        close(ep[0]);
        close(ep[1]);
        goto fail;
    }

    // fork 후 슬롯 등록이 끝나기 전에 SIGCHLD가 끼어드는 레이스 방지
    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block, &old);

    pid_t pid = fork();
    if (pid < 0) {
        sigprocmask(SIG_SETMASK, &old, NULL);
        close(op[0]);
        close(op[1]);
        close(ep[0]);
        close(ep[1]);
        close(xp[0]);
        close(xp[1]);
        goto fail;
    }

    if (pid == 0) {
        sigprocmask(SIG_SETMASK, &old, NULL);
        signal(SIGPIPE, SIG_DFL); // 서버가 무시로 바꿔둔 걸 exec 대상이 물려받지 않게
        int dn = open("/dev/null", O_RDONLY);
        if (dn >= 0) {
            dup2(dn, 0);
            if (dn > 2)
                close(dn);
        }
        dup2(op[1], 1);
        dup2(ep[1], 2);
        // 나머지 fd(소켓 포함)는 전부 CLOEXEC라 exec 때 닫힘
        execvp(argv[0], argv);
        int e = errno;
        write(xp[1], &e, sizeof(e));
        _exit(127);
    }

    close(op[1]);
    close(ep[1]);
    close(xp[1]);

    job *j = &jobs[slot];
    j->pid = pid;
    j->valid_vit = 1;
    j->seq = next_seq++;
    j->cmd = strdup(cmdline);
    j->readpipe = op[0];
    j->errpipe = ep[0];
    fcntl(op[0], F_SETFL, O_NONBLOCK);
    fcntl(ep[0], F_SETFL, O_NONBLOCK);

    // exec 성공 시 CLOEXEC로 xp가 닫혀 EOF, 실패 시 errno가 들어옴
    int e = 0;
    ssize_t r;
    do {
        r = read(xp[0], &e, sizeof(e));
    } while (r < 0 && errno == EINTR);
    close(xp[0]);
    if (r == (ssize_t)sizeof(e)) {
        char msg[128];
        j->exec_errno = e;
        snprintf(msg, sizeof(msg), "exec failed: %s\n", strerror(e));
        outbuf_append(&j->err, msg, strlen(msg));
    }

    sigprocmask(SIG_SETMASK, &old, NULL);
    dprintf(fd, "OK %d\n", slot);
    return;

fail:
    dprintf(fd, "ERR cannot start job\n");
}

// 숫자 job_id 파싱. 유효하면 0 반환
static int parse_job_id(const char *s, int *id) {
    if (!s)
        return -1;
    char *end;
    long v = strtol(s, &end, 10);
    if (*end != '\0' || end == s || v < 0 || v >= MAX_JOBS)
        return -1;
    *id = (int)v;
    return 0;
}

static void cmd_result(int id, int fd) {
    const char *st = job_status(id);
    if (!strcmp(st, "EMPTY")) {
        dprintf(fd, "ERR no such job\n");
    } else if (!strcmp(st, "RUNNING") || !strcmp(st, "KILLED")) {
        dprintf(fd, "%s\n", st);
    } else { // DONE / ERROR: 헤더 한 줄 + stdout raw + stderr raw
        job *j = &jobs[id];
        int code = WIFEXITED(j->wstatus) ? WEXITSTATUS(j->wstatus) : -1;
        dprintf(fd, "%s %d %zu %zu\n", st, code, j->out.len, j->err.len);
        write_all(fd, j->out.data, j->out.len);
        write_all(fd, j->err.data, j->err.len);
    }
}

static void cmd_kill(int id, int fd) {
    if (!jobs[id].valid_vit) {
        dprintf(fd, "ERR no such job\n");
    } else if (jobs[id].exited) {
        dprintf(fd, "ERR job already finished\n");
    } else {
        kill(jobs[id].pid, SIGTERM);
        dprintf(fd, "OK\n");
    }
}

void read_command(const char *line, int fd) {
    char *copy = strdup(line);
    if (!copy) {
        dprintf(fd, "ERR out of memory\n");
        return;
    }
    char *save;
    char *command = strtok_r(copy, " \t", &save);

    if (!command) {
        dprintf(fd, "ERR empty command\n");
    } else if (!strcmp(command, "LIST")) {
        for (int i = 0; i < MAX_JOBS; i++)
            if (jobs[i].valid_vit)
                dprintf(fd, "JOB %d %s %s\n", i, job_status(i), jobs[i].cmd);
        dprintf(fd, "END\n");
    } else if (!strcmp(command, "RESULT") || !strcmp(command, "KILL")) {
        int id;
        if (parse_job_id(strtok_r(NULL, " \t", &save), &id) < 0)
            dprintf(fd, "ERR no such job\n");
        else if (!strcmp(command, "RESULT"))
            cmd_result(id, fd);
        else
            cmd_kill(id, fd);
    } else if (!strcmp(command, "RUN")) {
        char *argv[MAX_ARGS + 1];
        int n = 0;
        char *tok;
        while ((tok = strtok_r(NULL, " \t", &save)) != NULL && n < MAX_ARGS)
            argv[n++] = tok;
        if (n == 0) {
            dprintf(fd, "ERR usage: RUN <command>\n");
        } else if (tok != NULL) {
            dprintf(fd, "ERR too many arguments\n");
        } else {
            argv[n] = NULL;
            // LIST에 보여줄 명령줄: 원본에서 "RUN" 뒤 공백을 건너뛴 나머지
            const char *rest = line + strspn(line, " \t");
            rest += strcspn(rest, " \t");
            rest += strspn(rest, " \t");
            fork_execute(argv, rest, fd);
        }
    } else {
        dprintf(fd, "ERR unknown command\n");
    }
    free(copy);
}
