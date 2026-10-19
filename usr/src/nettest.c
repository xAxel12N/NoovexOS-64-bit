#include "posix.h"
int main(int argc, char** argv){
    const char* host = argc>1 ? argv[1] : "example.com";
    printf("connecting to %s:80 ...\n", host);
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(s < 0){ printf("socket() failed\n"); return 1; }
    if(connect_host(s, host, 80) != 0){ printf("connect failed\n"); return 1; }
    printf("connected! sending GET\n");
    char req[128];
    snprintf(req, sizeof req, "GET / HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", host);
    send(s, req, strlen(req), 0);
    char buf[1024]; int n;
    int total=0;
    while((n = recv(s, buf, sizeof(buf)-1, 0)) > 0){
        buf[n]=0;
        for(int i=0;i<n && total<200;i++,total++) putchar(buf[i]);
        if(total>=200)break;
    }
    printf("\n...got %d+ bytes\n", total);
    closesocket(s);
    return 0;
}
