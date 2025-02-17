#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <sys/epoll.h>
#include <cstdio>
#include <cstdlib>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <cerrno>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include "locker.h"
#include <string.h>

class http_conn
{
public:
    static int m_epollfd;                      // 所有socket上的事件都被注册到同一个epoll对象上
    static int m_user_count;                   // 统计用户的数量
    static const int READ_BUFFER_SIZE = 2048;  // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 2048; // 写缓冲区的大小
    static const int FILENAME_LEN = 200;

    // http请求方法，只支持GET
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT
    };

    /*解析客户端请求时，主状态机的状态
    CHECK STATE REQUESTLINE：当前正在分析请求行
    CHECK STATE HEADER：当前正在分析头部字段
    CHECK_STATE_CONTENT：当前正在解析请求体*/
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    /*服务器处理HTTP请求的可能结果，报文解析的结果
    NO REQUEST：请求不完整，需要继续读取客尸数据
    GET REQUEST： 表示获得了一个完成的客户请求
    BAD REQUEST:表示客户请求语法诸误
    NO RESOURCE：表示服务器没有资源
    FORBIDDEN_REQUEST：表示客户对资源没有足够的访问权限
    FILE REQUEST：文件请求，获取文件成功
    INTERNAL ERROR：表示服务器内部错误
    CLOSED_CONNECTION：表示客户端已经关闭连接了*/
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    // 从状态机的三种可能状态，即行的读取状态，分别表示
    //  1.读取到一个完整的行2.行出错3.行数据尚目不完整
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

    http_conn() {}
    ~http_conn() {}

    void process();                                 // 处理客户端请求
    void init(int sockfd, const sockaddr_in &addr); // 初始化新的连接
    void close_conn();                              // 关闭连接
    bool read();                                    // 非阻塞的读
    bool write();                                   // 非阻塞的写

private:
    int m_sockfd;                      // 该HTTP连接的socket;
    sockaddr_in m_address;             // 通信的socket地址
    char m_read_buf[READ_BUFFER_SIZE]; // 读缓冲区
    int m_read_index;                  // 标记读缓冲区中以及客户端读入最后一个字节的下一个位置

    int m_checked_index;            // 当前分析的字符在读缓冲区的位置
    int m_start_line;               // 当前解析的字符的行的位置
    char m_real_file[FILENAME_LEN]; // 客户请求的目标文件的完整路径 其内容等于 doc_root + m_url, doc_root是网站根目录
    char *m_url;                    // 请求目标文件的文件名
    char *m_version;                // 协议版本 HTTP1.1
    METHOD m_method;                // 请求方法
    CHECK_STATE m_check_state;      // 主状态机当前所属的状态
    char *m_host;                   // 主机名
    int m_content_length;           //HTTP请求的消息总长度
    bool m_linger;                  // HTTp请求是否保持连接

    char *m_file_address;    // 客户请求的目标文件被mmap到内存中的起始位置
    struct stat m_file_stat; // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec m_iv[2];    // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iv_count;
    char m_write_buf[WRITE_BUFFER_SIZE]; // 写缓冲区
    int m_write_index;                   // 写缓冲区中待发送的字节数


    void init();                              // 初始化连接其余的数据
    bool process_write(HTTP_CODE ret);        // 填充HTTP应答
    HTTP_CODE process_read();                 // 解析HTTP请求
    HTTP_CODE parse_request_line(char *text); // 解析请求首行
    HTTP_CODE parse_headers(char *text);      // 解析请求头
    HTTP_CODE parse_content(char *text);      // 解析请求体

    // 这一组函数被process_write调用以填充HTTP应答。
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_content_type();
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

    char *get_line()
    {
        return m_read_buf + m_start_line;
    }
    HTTP_CODE do_request();

    LINE_STATUS parse_line();
};

#endif