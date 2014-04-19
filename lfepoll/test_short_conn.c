#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char *const arvg[]) {
    int i;
    char buff[1024];
    const char *hello = "hello world\n";
    int helloLen = strlen(hello);
    
    for (i = 0; i < 1000000; ++i) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
    
        struct sockaddr_in srvAddr;
        srvAddr.sin_family = AF_INET;
        srvAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        srvAddr.sin_port = htons(9876);
        
        if (connect(sock, (struct sockaddr *)&srvAddr, sizeof(srvAddr)) == -1) {
            perror("connect");
            close(sock);
            continue;
        }
        
        int size = 0;
        while (size < helloLen) {
            int ret = write(sock, hello + size, helloLen - size);
            if (ret > 0) size += ret;
        }
        recv(sock, buff, helloLen, MSG_WAITALL);
        close(sock);
    }

    return 0;
}
