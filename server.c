#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "job.h"

#define PORT 8080
#define BUF_SIZE 1024


#define MAX_CLIENTS 16
#define INPUT_CAP 4096

typedef struct {
    int fd;
    char input_buffer[INPUT_CAP];
    size_t input_len;
} client_t;

static client_t clients[MAX_CLIENTS];

static void init_clients(void){
    for(int i = 0; i<MAX_CLIENTS; i++){
        clients[i].fd = -1;
        clients[i].input_len = 0;
    }
}

static int add_client(int fd){
    for(int i = 0; i<MAX_CLIENTS; i++){
        if(clients[i].fd == -1){
            clients[i].fd = fd;
            clients[i].input_len = 0;
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
}

static int find_client_index(int fd){
    for (int i = 0; i<MAX_CLIENTS; i++){
        if(clients[i].fd == fd) return i;
    }
    return -1;
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
    
    //인자 길이 만큼 문자열 출력 법
    printf("[buffer] %.*s\n", (int)client->input_len, client->input_buffer);

    process_complete_lines(client_index);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    int server_fd;
    struct sockaddr_in server_addr;

    init_clients();
    
    init_jobs();
    install_sigchld_handler();
    //for select
    fd_set master_set, read_set;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
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
    server_addr.sin_addr.s_addr = INADDR_ANY;
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
    int current_max_fd = server_fd;

    printf("서버 대기 중...\n");

    while (1) {

        int current_max_fd = server_fd;

        //복사본 만들기
        read_set = master_set;

        for(int i = 0; i < MAX_CLIENTS; i++){
            if(clients[i].fd > current_max_fd) current_max_fd = clients[i].fd;
        }

        for(int i = 0; i<10; i++){
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
            int new_client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
            
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

            if(new_client_fd > current_max_fd) current_max_fd = new_client_fd;

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
        for(int i = 0; i<10; i++){
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