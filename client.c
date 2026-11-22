#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080

// 토큰은 환경변수 JOB_RUNNER_TOKEN으로 받는다 (인자로 받으면 ps에 노출됨)
int main(void) {
    const char *token = getenv("JOB_RUNNER_TOKEN");
    if (!token) {
        fprintf(stderr, "JOB_RUNNER_TOKEN 환경변수를 설정해야 함\n");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    dprintf(sock, "AUTH %s\n", token);

    // stdin -> 서버, 서버 -> stdout. 응답이 여러 줄/여러 조각이어도 그대로 출력
    struct pollfd fds[2] = {{STDIN_FILENO, POLLIN, 0}, {sock, POLLIN, 0}};
    char buf[4096];
    while (poll(fds, 2, -1) >= 0) {
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) { // stdin EOF: 보내기만 닫고 남은 응답은 끝까지 받음
                shutdown(sock, SHUT_WR);
                fds[0].fd = -1;
            } else {
                send(sock, buf, (size_t)n, 0);
            }
        }
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            ssize_t n = recv(sock, buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            fwrite(buf, 1, (size_t)n, stdout);
            fflush(stdout);
        }
    }

    close(sock);
    return 0;
}
