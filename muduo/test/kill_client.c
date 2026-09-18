// 复现 SIGPIPE 的取证客户端:触发服务端"对已 RST 的对端继续 sendfile"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(int argc, char** argv){
    const char* path = argc > 1 ? argv[1] : "/huge.bin";
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8000);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if(connect(fd, (struct sockaddr*)&addr, sizeof addr) < 0){ perror("connect"); return 1; }

    char req[256];
    snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: a\r\n\r\n", path);
    write(fd, req, strlen(req));

    char buf[100];
    ssize_t n = recv(fd, buf, sizeof buf, 0);   // 只读一点:确认服务端开始发送
    printf("recv %zd bytes\n", n);

    sleep(1);                                   // 服务端此刻卡在续传里(对端不收)

    struct linger lg;
    lg.l_onoff = 1;
    lg.l_linger = 0;
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof lg);
    close(fd);                                  // SO_LINGER=0 → 立即 RST
    printf("RST sent\n");
    return 0;
}
