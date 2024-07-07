#include <cstdio>
#include <cstdlib>
#include <cstring>
// #include<sys/socket.h>
// #include<netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65535        // 最大连接数
#define MAX_EVENT_NUM 10000 // 监听的最大事件数量

extern void addfd(int epollfd, int fd, bool one_shot); // 添加文件描述符到epoll中
extern void removefd(int epollfd, int fd);             // 从epoll中删除文件描述符
extern void modfd(int epollfd, int fd, int ev);         // 修改文件描述符

void addsig(int sig, void(handler)(int)) // 添加信号捕捉
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}
/*信号集初始化：
使用 sigfillset(&sa.sa_mask); 会阻塞所有信号，这通常不是你想要的行为，尤其是在信号处理函数中。通常，你只想阻塞那些可能会干扰当前信号处理过程的信号，或者在某些特殊情况下才需要阻塞所有信号。如果你没有特别的需求要阻塞所有信号，那么可以考虑使用 sigemptyset(&sa.sa_mask); 来初始化信号集为空，表示不阻塞任何信号。
错误处理：
sigaction 函数在失败时会返回 -1 并设置 errno。你的函数应该检查 sigaction 的返回值，并在失败时给出适当的错误处理或反馈。
函数参数和类型：
你的函数签名是正确的，但注释应该更清晰地说明参数的作用。
代码风格和可读性：
虽然这不是一个错误，但良好的代码风格和注释可以提高代码的可读性和可维护性。
    ————来自文心一言
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    if (sigaction(sig, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);*/

int main(int argc, char *argv[])
{
    if (argc <= 1)
    {
        printf("按照此格式：%s port_number\n", basename(argv[0]));
        exit(-1);
    }

    // get port
    int port = atoi(argv[1]);

    // 对SIGPIE信号进行处理
    addsig(SIGPIPE, SIG_IGN);

    // 创建和初始化线程池
    threadpool<http_conn> *pool = NULL;
    try
    {
        pool = new threadpool<http_conn>;
    }
    catch (...)
    {
        exit(-1);
    }

    http_conn *user = new http_conn[MAX_FD]; // 创建数组保存所有客户端的信息

    int listenfd = socket(AF_INET, SOCK_STREAM, 0); // 用于监听的套接字

    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)); // 端口复用

    // 绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    int ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    if (ret < 0)
    {
        perror("bind error\n");
        return -1;
    }

    // 监听
    ret = listen(listenfd, 5);
    if (ret < 0)
    {
        perror("listen error\n");
        return -1;
    }

    // 创建epoll对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUM];
    int epollfd = epoll_create(5);
    if (epollfd < 0)
    {
        perror("epoll create error\n");
        return -1;
    }

    // 将监听的文件描述符添加到epoll中
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while (1)
    {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUM, -1);
        if (num < 0 && errno != EINTR)
        {
            perror("epoll error\n");
            break;
        }
        // 循环遍历
        for (int i = 0; i < num; i++)
        {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) // 有客户端链接
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlen);
                if (connfd < 0)
                {
                    perror("accept error\n");
                    continue;
                }
                if (http_conn::m_user_count >= MAX_FD)
                {
                    // 连接数满
                    close(connfd);
                    continue;
                }
                // 将新的客户数据初始化，放到数组中
                user[connfd].init(connfd, client_address);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 客户端断开连接或异常错误
                user[sockfd].close_conn();
            }
            else if (events[i].events & EPOLLIN)
            {
                if (user[sockfd].read()) // 一次性读所有数据
                {
                    pool->append(user + sockfd);
                }
                else
                {
                    user[sockfd].close_conn();
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                if (!user[sockfd].write()) // 一次性写完所有数据
                {
                    user[sockfd].close_conn();
                }
            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete[] user;
    delete pool;
    return 0;
}