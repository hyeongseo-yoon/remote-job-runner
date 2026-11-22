#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "job.h"

#define PORT 8080
#define BUF_SIZE 1024


#define MAX_CLIENTS 16
#define INPUT_CAP 4096
#define MIN_TOKEN_LEN 16

static char auth_token[128];

typedef struct {
    int fd;
    char input_buffer[INPUT_CAP];
    size_t input_len;
    int authed;
} client_t;

static client_t clients[MAX_CLIENTS];

static void init_clients(void){
    for(int i = 0; i<MAX_CLIENTS; i++){
        clients[i].fd = -1;
        clients[i].input_len = 0;
        clients[i].authed = 0;
    }
}

static int add_client(int fd){
    for(int i = 0; i<MAX_CLIENTS; i++){
        if(clients[i].fd == -1){
            clients[i].fd = fd;
            clients[i].input_len = 0;
            clients[i].authed = 0;
            return i;
        }
    }
    return -1;
}

static void close_client(int client_index){
    if(clients[client_index].fd >= 0){
        close(clients[client_index].fd);
    }
    clients[client_index].fd = -1;
    clients[client_index].input_len = 0;
    clients[client_index].authed = 0;
}

// 길이/내용 모두 시간차 없이 비교
static int token_equal(const char *a, const char *b){
    size_t la = strlen(a), lb = strlen(b);
    unsigned char diff = (la != lb);
    for(size_t i = 0; i < lb; i++) diff |= (unsigned char)(a[i < la ? i : 0] ^ b[i]);
    return diff == 0;
}

// JOB_RUNNER_TOKEN 환경변수를 쓰고, 없으면 랜덤 토큰을 만들어서 출력
static int init_token(void){
    const char *env = getenv("JOB_RUNNER_TOKEN");
    if(env){
        size_t len = strlen(env);
        if(len < MIN_TOKEN_LEN || len >= sizeof(auth_token)){
            fprintf(stderr, "JOB_RUNNER_TOKEN 길이는 %d~%zu자여야 함\n", MIN_TOKEN_LEN, sizeof(auth_token) - 1);
            return -1;
        }
        memcpy(auth_token, env, len + 1);
        return 0;
    }
    unsigned char rnd[16];
    if(getrandom(rnd, sizeof(rnd), 0) != (ssize_t)sizeof(rnd)){
        perror("getrandom");
        return -1;
    }
    for(size_t i = 0; i < sizeof(rnd); i++) snprintf(auth_token + i * 2, 3, "%02x", rnd[i]);
    printf("토큰 생성됨: %s\n", auth_token);
    return 0;
}

// 개행 문자를 중심으로 한줄을 잘라내서 파싱하는 함수
static void process_complete_lines(int client_index){
    client_t *client = &clients[client_index];

    while(1){
        char *newline = memchr(client->input_buffer, '\n', client->input_len);
        
        if(newline == NULL) return;

        size_t line_len = (size_t)(newline - client->input_buffer);
        
        //윈도우 환경 명령어 파싱도 고려('\r')
        if(line_len > 0 && client->input_buffer[line_len-1] == '\r') line_len--;
        
        char line[INPUT_CAP];
        
        memcpy(line, client->input_buffer, line_len);
        line[line_len] = '\0';
    
        //이후 남은 버퍼에서 \n기준까지 제거하고, 앞쪽으로 데이터를 옮기는 작업
        
        size_t consumed = (size_t)(newline-client->input_buffer) + 1;

        memmove(client->input_buffer, client->input_buffer + consumed, client->input_len-consumed);
    
        client->input_len -= consumed;

        if(!client->authed){
            if(strncmp(line, "AUTH ", 5) == 0 && token_equal(line + 5, auth_token)){
                client->authed = 1;
                dprintf(client->fd, "OK\n");
                continue;
            }
            dprintf(client->fd, "ERR auth failed\n");
            close_client(client_index);
            return;
        }

        read_command(line, client->fd);
    }
}
//인자로 받은 client_fd로부터 recv를 통해 받은 문자열 검사에 따른 프로토콜 진행
static void handle_client_input(int client_index){
    client_t *client = &clients[client_index];
    char buffer[BUF_SIZE];

    ssize_t n = recv(client->fd, buffer, sizeof(buffer), 0);

    //0일 경우 연결 종료
    if(n == 0){
        printf("연결 종료: index = %d, fd=%d\n", client_index, client->fd);
        close_client(client_index);
        return;
    }
    //음수일 경우 오류
    if(n<0){
        perror("recv");
        close_client(client_index);
        return;
    }

    //이후 n>0일때
    //만약 용량이 초과일 시 오류 처리
    if(client->input_len + (size_t)n > INPUT_CAP){
        printf("입력 버퍼 초과: fd=%d\n", client->fd);
        close_client(client_index);
        return;
    }
    
    //가능할 시, 이어 붙이기
    memcpy(client->input_buffer + client->input_len, buffer, (size_t)n);
    client->input_len += (size_t)n;

    printf("[recv] index=%d fd=%d received=%zd, total=%zu\n", client_index, client->fd, n, client->input_len);
    
    process_complete_lines(client_index);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    int server_fd;
    struct sockaddr_in server_addr;

    if(init_token() < 0) return 1;
    init_clients();
    
    init_jobs();
    install_sigchld_handler();
    //for select
    fd_set master_set, read_set;

    server_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    //서버 재실행 편하게 만드는 개발 편의 설정
    int opt = 1;
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 인증 있어도 외부 노출 금지
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    
    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    FD_ZERO(&master_set);
    FD_SET(server_fd, &master_set);
    printf("서버 대기 중... (127.0.0.1:%d)\n", PORT);

    while (1) {

        int current_max_fd = server_fd;

        //복사본 만들기
        read_set = master_set;

        for(int i = 0; i < MAX_CLIENTS; i++){
            if(clients[i].fd > current_max_fd) current_max_fd = clients[i].fd;
        }

        for(int i = 0; i<MAX_JOBS; i++){
            if(jobs[i].valid_vit != 1) continue;

            if(jobs[i].readpipe >= 0){
                FD_SET(jobs[i].readpipe, &read_set);
                
                if(jobs[i].readpipe > current_max_fd) current_max_fd = jobs[i].readpipe;
            }

            if(jobs[i].errpipe >= 0){
                FD_SET(jobs[i].errpipe, &read_set);

                if(jobs[i].errpipe > current_max_fd) current_max_fd = jobs[i].errpipe;
            }
        }

        int ready_count = select(current_max_fd + 1, &read_set, NULL, NULL, NULL);
        
        if(ready_count < 0){
            if(errno == EINTR) continue;
            perror("select");
            break;
        }

        //1. 새 client 접속일 시 
        if(FD_ISSET(server_fd, &read_set)){
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_client_fd = accept4(server_fd, (struct sockaddr *)&client_addr, &client_len, SOCK_CLOEXEC);
            
            if(new_client_fd < 0){
                perror("accept");
                continue;
            }

            int client_index = add_client(new_client_fd);

            if(client_index < 0){
                printf("client limit reached\n");
                close(new_client_fd);
                continue;
            }

            FD_SET(new_client_fd, &master_set);

            printf("접속: index=%d, fd=%d, ip=%s\n", client_index, new_client_fd, inet_ntoa(client_addr.sin_addr));
        }
        //2. client socket 요청 처리
        for(int i = 0; i< MAX_CLIENTS; i++){
            int client_fd = clients[i].fd;

            if(client_fd <0) continue;

            if(FD_ISSET(client_fd, &read_set)){
                handle_client_input(i);
                if(clients[i].fd <0){
                    FD_CLR(client_fd, &master_set);
                }
            }
        }
        //3. job stdout/stderr pipe 처리
        for(int i = 0; i<MAX_JOBS; i++){
            if(jobs[i].valid_vit !=1) continue;

            if(jobs[i].readpipe >=0 && FD_ISSET(jobs[i].readpipe, &read_set)){
                read_job_output(i, 0);
            }
            if(jobs[i].errpipe >=0 && FD_ISSET(jobs[i].errpipe, &read_set)){
                read_job_output(i, 1);
            }
        }

    }
    close(server_fd);
    return 0;
}