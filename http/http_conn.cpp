#include "http_conn.h"
#include "../log/log.h"
#include <map>
#include <mysql/mysql.h>
#include <fstream>

/* 引用，实现gbk和utf8互相转换 */
#include <iconv.h>

// #define connfdET //边缘触发非阻塞
#define connfdLT //水平触发阻塞

//#define listenfdET //边缘触发非阻塞
#define listenfdLT //水平触发阻塞

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

//当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
const char *doc_root = "./new_root";

//将表中的用户名和密码放入map
map<string, string> users;
locker m_lock;
string http_conn::tmp = "";
void http_conn::initmysql_result(connection_pool *connPool)
{
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);
    mysql_set_character_set(mysql, "utf8");

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
        // LOG_INFO("oooooooooooooooooooooo%s", temp2);
    }
    m_lock.lock();
    if(tmp==""){
        if (mysql_query(mysql, "SELECT title, author, videoTime FROM music"))
        {
            LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
        }
        
        //从表中检索完整的结果集
        MYSQL_RES *result1 = mysql_store_result(mysql);

        //返回结果集中的列数
        int num_fields1 = mysql_num_fields(result1);

        //返回所有字段结构的数组
        MYSQL_FIELD *fields1 = mysql_fetch_fields(result1);

        //从结果集中获取下一行，将对应的用户名和密码，存入map中
        while (MYSQL_ROW row = mysql_fetch_row(result1))
        {
            string temp1(row[0]);
            string temp2(row[1]);
            string temp3(row[2]);
            // LOG_INFO("oooooooooooooooooooooo%s", temp1);
            tmp += temp1 + '\t' + temp2 + '\t' + temp3 + "\r\n";
        }
    }
    m_lock.unlock();
    // cout<<"this"<<endl;
}

//对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;

#ifdef connfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef connfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

#ifdef listenfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef listenfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;

#ifdef connfdET
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
#endif

#ifdef connfdLT
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    //int reuse=1;
    //setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    init();
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_checked_idx_content = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_string = NULL;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

//从状态机，用于分析出一行内容
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx) {
                // LOG_INFO("parse line-LINE_OPEN - %s", temp);
                return LINE_OPEN;
            }
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                // LOG_INFO("parse line-LINE_OK - %s", temp);
                return LINE_OK;
            }
            // LOG_INFO("parse line-LINE_BAD - %s", temp);
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                // LOG_INFO("parse line-LINE_OK - %s", temp);
                return LINE_OK;
            }
            // LOG_INFO("parse line-LINE_BAD - %s", temp);
            return LINE_BAD;
        }
    }
    // LOG_INFO("parse line-LINE_OPEN - %s", temp);
    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once(){
    if (m_read_idx >= READ_BUFFER_SIZE)  return false;
    int bytes_read = 0;

#ifdef connfdLT
    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
    m_read_idx += bytes_read;

    if (bytes_read <= 0){
        return false;
    }
    LOG_INFO("\n\n------new_conn-----\nm_read_idx:%d", m_read_idx);
    return true;

#endif

#ifdef connfdET
    while (true)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        LOG_INFO("bytes_read:%d", bytes_read);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return false;
        }
        else if (bytes_read == 0)
        {
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
#endif
}

//解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    LOG_INFO("parse request line -- %s", text);
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if ((strcasecmp(m_version, "HTTP/1.1") != 0) && (strcasecmp(m_version, "HTTP/1.0") != 0))
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "main.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    LOG_INFO("parse headers -- %s", text);
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        //printf("oop!unknow header: %s\n",text);
        // LOG_INFO("%s\toop!unknow header!", text);
        Log::get_instance()->flush();
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{    
    LOG_INFO("parse content -- %s", text);
    if (m_content_length<1000&&m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    if (m_content_length>=1000&&m_read_idx >= (m_content_length + m_checked_idx_content))
    {
        m_string = get_content();
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_checked_idx;
        if((strlen(text))<1000) {
            LOG_INFO("%s", text);
            Log::get_instance()->flush();
        }

        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {   
            if(m_checked_idx_content==0) 
                m_checked_idx_content = m_checked_idx;
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

static const std::string base64_chars =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789+/";
#define IMG_JPG "data:image/png;base64,"	//jpg图片信息，其他类似
static inline bool is_base64(const char c)
{
    return (isalnum(c) || (c == '+') || (c == '/'));
}


string http_conn::base64_encode(const char * bytes_to_encode, unsigned int in_len)
{
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--)
    {
        char_array_3[i++] = *(bytes_to_encode++);
        if(i == 3)
        {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for(i = 0; (i <4) ; i++)
            {
                ret += base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }
    if(i)
    {
        for(j = i; j < 3; j++)
        {
            char_array_3[j] = '\0';
        }

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for(j = 0; (j < i + 1); j++)
        {
            ret += base64_chars[char_array_4[j]];
        }

        while((i++ < 3))
        {
            ret += '=';
        }

    }
    return ret;
}

string http_conn::Decode(string const & encoded_string)
{
    int in_len = (int) encoded_string.size() - 2;
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string ret;

    while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
        char_array_4[i++] = encoded_string[in_]; in_++;
        if (i ==4) {
            for (i = 0; i <4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; (i < 3); i++)
                ret += char_array_3[i];
            i = 0;
        }
    }
    if (i) {
        for (j = i; j <4; j++)
            char_array_4[j] = 0;

        for (j = 0; j <4; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
    }

    return ret;
}


unsigned char ToHex(unsigned char x) 
{ 
    return  x > 9 ? x + 55 : x + 48; 
}
 
unsigned char FromHex(unsigned char x) 
{ 
    unsigned char y;
    if (x >= 'A' && x <= 'Z') y = x - 'A' + 10;
    else if (x >= 'a' && x <= 'z') y = x - 'a' + 10;
    else if (x >= '0' && x <= '9') y = x - '0';
    else assert(0);
    return y;

}

std::string UrlDecode(const std::string& str)
{
    std::string strTemp = "";
    size_t length = str.length();
    for (size_t i = 0; i < length; i++)
    {
        if (str[i] == '+') strTemp += ' ';
        else if (str[i] == '%')
        {
            assert(i + 2 < length);
            unsigned char high = FromHex((unsigned char)str[++i]);
            unsigned char low = FromHex((unsigned char)str[++i]);
            strTemp += high*16 + low;
        }
        else strTemp += str[i];
    }
    /* --------test-------- */
    // printf("%s\n", strTemp);
    // printf("d - %d\n", strTemp);
    // printf("%s\n", strTemp.c_str());
    // printf("d - %d\n", strTemp.c_str());
    LOG_INFO("UrlDecode - strTemp: %s", strTemp);
    return strTemp;
}


http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);      /* 开始解析请求资源位置，先将资源底层目录添加到m_real_file */
    /* --------test-------- */
    LOG_INFO("do request - m_real_file: %s\n", m_real_file);
    int len = strlen(doc_root);
    const char *p = strrchr(m_url, '/');
    /* --------test-------- */
    LOG_INFO("do request - m_url: %s\n", m_url);    /* http请求中，前端指明了请求资源所在的相对路径*/
    LOG_INFO("do request - p: %s\n", p);        /* 根据最后一个/位置，切分出请求资源url编码后的文件名，等待进行解析 */
    LOG_INFO("cgi: %d\n", cgi);                 /* http请求方法 */
    
    if(cgi==1&& *(p + 1) == 'u'){
        char *p = strrchr(m_string, '?');
        /* --------test-------- */
        LOG_INFO("do request - p+1 == u - m_string: %s\n", m_string);
        LOG_INFO("do request - p: %s\n", p);
        int ind = 1;
        string filename;
        while(p[ind]!='/'){
            filename += p[ind];
            ind++;
        }
        /* --------test-------- */
        LOG_INFO("do request - filename: %s\n", filename);
        p = strrchr(m_string, ',');
        p += 1; 
        m_file_address = p;
        /* --------test-------- */
        LOG_INFO("do request - m_file_address: %s\n", m_file_address);

        string img_data = Decode(p);
        std::ofstream fout("./new_root/modifyavatar/" + filename + ".png", std::ios::binary);
        fout.write(img_data.c_str(), img_data.size());
        fout.close();
        return POST_REQUEST;
    }
    else if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {

        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        /* --------test-------- */
        LOG_INFO("do request - m_url_real: %s\n", m_url_real);
        LOG_INFO("do request - m_real_file: %s\n", m_real_file);
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&password=123
        char name[100], password[100];
        int i;
        LOG_INFO("do request - 登录还是注册? m_string: %s\n", m_string);
        for (i = 9; m_string[i] != '&'; ++i)
            name[i - 9] = m_string[i];
        name[i - 9] = '\0';
        memcpy(m_name, name, i+1);
        LOG_INFO("do request - m_name: %s\n", m_name);

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';
        // LOG_INFO("password: %s", password);
        //同步线程登录校验
        // if (*(p + 1) == '3')
        if (strcmp(p, "/log/3CGISQL.cgi") == 0)
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())
            {

                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res){   
                    m_file_address = "yes!!";
                    return POST_REQUEST;
                }
                else{   
                    m_file_address = "yes!!";
                    return POST_REQUEST;
                }
            }
            else{   
                m_file_address = "no!!";
                return POST_REQUEST;
            }
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        // else if (*(p + 1) == '2')
        else if (strcmp(p, "/log/2CGISQL.cgi"))
        {   
            if (users.find(name) != users.end() && users[name] == password){   
                string filename = name;
                filename = "./new_root/modifyavatar/"+filename+".png";
                /* --------test-------- */
                LOG_INFO("do request - filename: %s", filename);
                char p[200];
                for(i=0; i<filename.length(); ++i)
                    p[i] = filename[i];
                if (stat(p, &m_file_stat) < 0){
                    char ch[READ_BUFFER_SIZE]; 
                    string t = tmp + "?no!!";
                    strcpy(ch, t.c_str());
                    m_file_address = ch;
                    return POST_REQUEST;
                }
                char ch[READ_BUFFER_SIZE];
                string t = tmp + "?yes!!"; 
                strcpy(ch, t.c_str());
                m_file_address = ch;
                return POST_REQUEST;
                // filename = "/root/fgz/TinyWebServer-raw_version/root/icon/yinfu01.png";
                // std::ifstream fin(filename, std::ios::binary);
                // fin.seekg(0, ios::end);
                // int iSize = fin.tellg();
                // char* szBuf = new (std::nothrow) char[iSize];
                // fin.seekg(0, ios::beg);
                // fin.read(szBuf, sizeof(char) * iSize);
                // fin.close();
                // string img = "data:image/png;base64,";
                // img += base64_encode(szBuf, iSize);
                // char ch[READ_BUFFER_SIZE];
                // strcpy(ch, img.c_str());
                // m_file_address = ch;
                // m_file_address = "yes!!";
                // return POST_REQUEST;
            }
            else{   
                m_file_address = "no!!";
                return POST_REQUEST;
            }
        }
        LOG_INFO("登录or注册判断完成!");
    }
    LOG_INFO("判断p+1,p+2--p: %s\n", p);

    if (*(p + 1) == 'u'&&*(p + 2) == 's')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/user_center.html");
        /* --------test-------- */
        LOG_INFO("do request - us - m_real_file: %s", m_real_file);
        printf("do request - us - m_real_file: %s", m_real_file);
        LOG_INFO("p+1 == u -- m_url: %s", m_url);
        LOG_INFO("p+1 == u -- p: %s\n", p);
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        /* --------test-------- */
        LOG_INFO("do request - us - m_real_file: %s", m_real_file);
        printf("do request - us - m_real_file: %s", m_real_file);
        free(m_url_real);
    }
    else if(*(p + 1) != 'u'){    
        char *p = strrchr(m_url, '%');
        if(p!=NULL) {
            string temp = UrlDecode(m_url);
            /* --------test-------- */
            LOG_INFO("p+1 != u -- m_url: %s", m_url);
            LOG_INFO("此时的p是为了判断是否有url的base64编码--p+1 != u -- p: %s\n", p);
            // LOG_INFO("---------------------: %s", temp);
            LOG_INFO("---------------------: %s", temp.c_str());
            char ch[100];
            strcpy(ch, temp.c_str());
            strncpy(m_real_file + len, ch, FILENAME_LEN - len - 1);
        }
        else strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }


    LOG_INFO("开始判断 ----- m_real_file: %s\n", m_real_file);

    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    
    /* --------test-------- */
    LOG_INFO("判断完毕 ----- m_real_file: %s\n", m_real_file);
    int fd = open(m_real_file, O_RDONLY);
    /* mmap 进程可以像读写内存一样对普通文件的操作 详细可看NUP2-12.2 */
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}
/* 解除映射 */
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write(){
    int temp = 0;

    if (bytes_to_send == 0){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1){
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp < 0){
            /* 如果TCP写缓冲没有空间，则等待下一批 EPOLLOUT 事件。虽然在此期间，服务器无法立即接收到同一客户的下一个请求，但这可以保证连接的完整性 */
            if (errno == EAGAIN){
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= m_iv[0].iov_len){
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0){
            /* 发送 HTTP 响应成功，根据 HTTP 请求中的 Connection 字段决定是否立即关闭连接 */
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_linger){
                init();
                return true;
            }
            else return false;
        }
    }
}


/* 往写缓冲中写入待发送的数据 */
bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    LOG_INFO("function: add_response - request:%s\n", m_write_buf);
    Log::get_instance()->flush();
    return true;
}


bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}


bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}


bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}


bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}


bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}


bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}


bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}


bool http_conn::process_write(HTTP_CODE ret)
{   
    // m_read_idx = 0;
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            LOG_INFO("FILE_REQUEST时的文件大小m_iv[1].iov_len: %d\n", m_file_stat.st_size);
            LOG_INFO("FILE_REQUEST时的m_write_buf大小m_iv[0].iov_len: %d\n", m_write_idx);
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    case POST_REQUEST:
    {
        add_status_line(200, ok_200_title);
        {   
            int lent =  strlen(m_file_address) - 2;
            add_headers(lent);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = lent;
            LOG_INFO("POST_REQUEST时的m_file_address: %s\n", m_file_address);
            m_iv_count = 2;
            bytes_to_send = m_write_idx + lent;
            return true;
        }
    }
    default:
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}


void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
    //  m_start_line = 0;
    // m_checked_idx = 0;
    // m_read_idx = 0;
    // init();
}
