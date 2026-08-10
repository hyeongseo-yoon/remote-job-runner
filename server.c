#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8080
#define BUF_SIZE 1024

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUF_SIZE];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
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
    printf("서버 대기 중...\n");

    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("accept");
        close(server_fd);
        return 1;
    }    
    printf("클라이언트 접속됨: %s\n", inet_ntoa(client_addr.sin_addr));
    
    while (1) {
        int n = recv(client_fd, buffer, BUF_SIZE, 0);
        if (n == 0){
            printf("클라이언트 연결 종료\n");
            break;
        }
        if (n<0){
            perror("recv");
            break;
        }
        buffer[n] = '\0';
        printf("받은 메시지: %s", buffer);

        if (send(client_fd, buffer, n, 0) < 0) {
            perror("send");
            break;
        }
    }

    close(client_fd);
    close(server_fd);
    return 0;
}