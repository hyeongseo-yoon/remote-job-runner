#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8080
#define BUF_SIZE 1024

int main(){
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUF_SIZE];

    sock = socket(AF_INET, SOCK_STREAM, 0);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }
    while (1) {
        printf("보낼 메세지: ");
        fgets(buffer, BUF_SIZE, stdin);
        send(sock, buffer, strlen(buffer), 0);

        int n = recv(sock, buffer, BUF_SIZE, 0);
        buffer[n] = '\0';
        printf("서버 응답: %s", buffer);
        if (strcmp(buffer, "exit\n") == 0) {
            break;
        }
    }

    close(sock);
    return 0;
}