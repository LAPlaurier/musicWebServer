#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 1024*1024;
    static const int WRITE_BUFFER_SIZE = 1024;
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION,
        POST_REQUEST
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr);
    void close_conn(bool real_close = true);
    void process();
    bool read_once();
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);

private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; };
    char *get_content() { return m_read_buf + m_checked_idx_content; };
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
    string Decode(string const & encoded_string);
    string base64_encode(const char * bytes_to_encode, unsigned int in_len);

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    static string tmp;

private:
    char m_name[100];   // bfq
    int m_sockfd;   /* 该HTTP连接的socket 和 对方的socket地址 */
    sockaddr_in m_address;

    /* 读缓冲区 */
    char m_read_buf[READ_BUFFER_SIZE];
    long long m_read_idx;   /* 标识读缓冲中已经读入的客户数据的最后一个字节的下一个位置 */ // bfq 此类型从int改为long long
    int m_checked_idx;   /* 当前正在分析的字符在读缓冲区中的位置 */
    int m_checked_idx_content;  /* bfq */
    int m_start_line;   /* 当前正在解析的行的起始位置 */
    char m_write_buf[WRITE_BUFFER_SIZE];    /* 写缓冲区 */
    int m_write_idx;    /* 写缓冲区中待发送的字节数 */


    CHECK_STATE m_check_state;  /* 主状态机当前所处的状态 */
    METHOD m_method;    /* 请求方法 */


    char m_real_file[FILENAME_LEN]; /* 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录 */
    char *m_url; /* 客户请求的目标文件的文件名 */
    char *m_version; /* HTTP协议版本号，仅支持 HTTP/1.1  */
    char *m_host; /* 主机名 */
    int m_content_length; /* HTTP请求的消息体的长度 */
    bool m_linger; /* HTTP请求是否要求保持连接 */


    char *m_file_address;   /* 客户请求的目标文件被mmap到内存中的起始位置 */
    struct stat m_file_stat;    /* 目标文件的状态，通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息 */

    struct iovec m_iv[2]; /* 采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量 */
    int m_iv_count;



    int cgi;        //是否启用的POST
    char *m_string; //存储请求头数据
    int bytes_to_send;
    int bytes_have_send;
};

#endif
