#include "../http/http_conn.h"
#include "http_conn.h"

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

locker m_lock;
map<string, string> users;

http_conn::http_conn() {

}

http_conn::~http_conn() {
    
}

void http_conn::initmysql(connection_pool *connpool)
{
    //先从是数据库连接池中取出一个连接
    MYSQL* mysql = NULL;
    connectionRAII mysqlcon(&mysql, connpool);

    //在user表中检索username，passwd数据，浏览器端输入
    if(mysql_query(mysql,"SELECT username,passwd FROM user")){
        LOG_ERROR("SELECT error:%s\n",mysql_error(mysql));
    }

    //在表中检索完整的结果集
    MYSQL_RES* result =  mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD* fields = mysql_fetch_field(result);

    //从结果集中获取下一行，将对应的用户名和密码存入 map 中
    while(MYSQL_ROW row = mysql_fetch_row(result)){
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;

    bytes_to_send = 0;
    bytes_have_send = 0;

    m_check_state = CHECK_STATE_REQUESTLINE;// 初始状态为检查请求行
    m_linger = false;// 默认不保持链接  Connection : keep-alive保持连接

    m_method = GET;// 默认请求方式为GET
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

// 对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
    return old_flag;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode){
    epoll_event event;
    event.data.fd = fd;

    if(TRIGMode == 1){
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    }else{
        event.events = EPOLLIN | EPOLLRDHUP;
    }
    if(one_shot){
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    //设置文件描述符非阻塞
    setnonblocking(fd);
}

//从内核时间表删除描述符
void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT，确保下一次可读时，EPOLLIN 事件能被触发     修改文件描述符
void modfd(int epollfd, int fd, int ev, int TRIGMode){
    epoll_event event;
    event.data.fd = fd;

    if(TRIGMode == 1){
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    }else{
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,int close_log, 
                     string user, string passwd, string sqlname){
    m_sockfd = sockfd;
    m_address = addr;

    //添加到 epoll 对象中
    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;//总用户数+1

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

//关闭连接
void http_conn::close_coon(bool real_close)
{
    if(real_close && m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;//关闭一个连接，客户总数量-1
    }
}

//从状态机，用于分析出一行内容  判断依据\r\n
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    // 遍历读取缓冲区中的数据
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        // 如果遇到回车符
        if (temp == '\r')
        {
            // 如果回车符后面没有字符了，则表示这一行还不完整，返回LINE_OPEN
            if ((m_checked_idx + 1) == m_read_idx){
                return LINE_OPEN;
            }else if (m_read_buf[m_checked_idx + 1] == '\n'){// 如果回车符后面是换行符，则表示这一行完整，将回车换行符替换为结束符，并返回LINE_OK
                m_read_buf[m_checked_idx++] = '\0';// 将回车符替换为结束符
                m_read_buf[m_checked_idx++] = '\0';// 将换行符替换为结束符
                return LINE_OK;
            }
            // 如果回车符后面不是换行符，则表示格式错误，返回LINE_BAD
            return LINE_BAD;
        }else if (temp == '\n'){ // 如果遇到换行符
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r'){
                // 如果换行符前面是回车符，则表示这一行完整，将回车换行符替换为结束符，并返回LINE_OK
                m_read_buf[m_checked_idx - 1] = '\0';// 将回车符替换为结束符
                m_read_buf[m_checked_idx++] = '\0';// 将换行符替换为结束符
                return LINE_OK;
            }
            // 如果换行符前面不是回车符，则表示格式错误，返回LINE_BAD
            return LINE_BAD;
        }
    }
    // 如果遍历完仍未找到回车换行符，则表示这一行还不完整，返回LINE_OPEN
    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once(){
    // 如果读取的数据长度已经超过了读缓冲区的大小，则返回false
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

    //LT读取数据
    if (m_TRIGMode == 0)
    {
        // 一次性读取尽可能多的数据到读缓冲区
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;// 更新已读取数据的长度

        // 如果读取的字节数小于等于0，则表示对方关闭连接，返回false
        if (bytes_read <= 0)
        {
            return false;
        }

        return true;// 读取成功，返回true
    }else{  // ET 读数据
        while (true){
            // 从 m_read_buf + m_read_idx 索引出开始保存数据，大小是 READ_BUFFER_SIZE - m_read_idx
            // 循环读取数据到读缓冲区，直到暂时没有数据可读
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            // 如果读取出错
            if (bytes_read == -1){
                //如果错误是因为缓冲区没有数据可读，则跳出循环
                if (errno == EAGAIN || errno == EWOULDBLOCK){
                    // 没有数据
                    break;
                }
                // 否则返回false
                return false;
            }else if (bytes_read == 0) {// 对方关闭连接
                return false;
            }
            m_read_idx += bytes_read;// 更新已读取数据的长度
        }
        return true;// 读取成功，返回true
    }
}

// 解析HTTP请求行，获得请求方法，目标URL,以及HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t"); // 判断第二个参数中的字符哪个在text中最先出现
    if (! m_url) { 
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';    // 置位空字符，字符串结束符
    char* method = text;
    if ( strcasecmp(method, "GET") == 0 ) { // 忽略大小写比较
        m_method = GET;
    }else if(strcasecmp(method, "POST") == 0){
        m_method = POST;
        cgi = 1;
    }else {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标
    m_url += strspn(m_url, " \t");
    m_version = strpbrk( m_url, " \t" );
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if (strcasecmp( m_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }

    /**
     * http://192.168.188.128:10000/index.html
     * https://192.168.188.128:10000/index.html
    */
    if (strncasecmp(m_url, "http://", 7) == 0 ) {   
        m_url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr( m_url, '/' );
    }
    if (strncasecmp(m_url, "https://", 8) == 0 ) {   
        m_url += 8;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr( m_url, '/' );
    }

    if ( !m_url || m_url[0] != '/' ) {
        return BAD_REQUEST;
    }
    //当url为/时，显示判断界面
    if (strlen(m_url) == 1){
        strcat(m_url, "judge.html");
    }
        
    m_check_state = CHECK_STATE_HEADER; // 检查状态变成检查头
    return NO_REQUEST;
}

// 解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {   
    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 ) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

// 我没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_content( char* text ) {
    if ( m_read_idx >= ( m_content_length + m_checked_idx ) )
    {
        text[ m_content_length ] = '\0';

        //POST请求中最后为输入的用户名和密码
        m_string = text;

        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 主状态机，解析请求
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
                || ((line_status = parse_line()) == LINE_OK)) {
        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        switch ( m_check_state ) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line( text );
                if ( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers( text );
                if ( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                } else if ( ret == GET_REQUEST ) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content( text );
                if ( ret == GET_REQUEST ) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request(){
    strcpy(m_real_file, doc_root);// 将文档根目录路径拷贝到实际文件路径中
    int len = strlen(doc_root);// 获取文档根目录路径长度
    //printf("m_url:%s\n", m_url);
    const char *p = strrchr(m_url, '/');// 在请求的 URL 中查找最后一个 '/' 字符的位置

    //处理cgi   数据库
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {

        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        // 构造实际 URL，去除标志部分
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //user = 123 & passwd = 123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i){
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j) {
            password[j] = m_string[i];
        }
        password[j] = '\0';

        if (*(p + 1) == '3') {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end()) {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res){
                    strcpy(m_url, "/log.html");
                }else{
                    strcpy(m_url, "/registerError.html");
                }
            }else{
                strcpy(m_url, "/registerError.html");
            }  
        }else if (*(p + 1) == '2'){
            //如果是登录，直接判断
            //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
            if (users.find(name) != users.end() && users[name] == password){
                strcpy(m_url, "/welcome.html");
            }else{
                strcpy(m_url, "/logError.html");
            }
        }
    }

    // 根据请求的 URL 标志选择相应的页面
    if (*(p + 1) == '0'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }else if (*(p + 1) == '1'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }else if (*(p + 1) == '5'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }else if (*(p + 1) == '6'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }else if (*(p + 1) == '7'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }else{
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    // 检查请求的文件是否存在
    if (stat(m_real_file, &m_file_stat) < 0){
        return NO_RESOURCE;
    }

    // 检查文件权限
    if (!(m_file_stat.st_mode & S_IROTH)){
        return FORBIDDEN_REQUEST;
    }

    // 检查是否是目录
    if (S_ISDIR(m_file_stat.st_mode)){
        return BAD_REQUEST;
    }

    // 打开文件并映射到内存
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 对内存映射区执行munmap操作   解除内存映射区
void http_conn::unmap() {
    // 检查 m_file_address 是否为非空指针，表示内存映射区存在
    if( m_file_address ){
        munmap( m_file_address, m_file_stat.st_size );
        //将 m_file_address 置为 0，表示映射区已解除
        m_file_address = 0;
    }
}

// 写HTTP响应
bool http_conn::write() {
    int temp = 0;
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束
        modfd( m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode); 
        init();
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);

        //发生错误
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) { //写缓冲区已满，等待下一次可写事件
                modfd( m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }

        //更新已发送字节数 bytes_have_send 和剩余待发送字节数 bytes_to_send
        bytes_have_send += temp;
        bytes_to_send -= temp;

        //如果已发送字节数大于等于当前缓冲区长度，更新 m_iv 中的数据指针和长度，以便发送下一个缓冲区的数据
        if (bytes_have_send >= m_iv[0].iov_len){
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        //如果待发送字节数为 0，表示数据已全部发送完毕，解除内存映射区
        if (bytes_to_send <= 0) {
            // 没有数据要发送了
            unmap();
            //将套接字注册为可读事件
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            //如果需要保持连接，则重置连接状态并返回 true，否则直接返回 false
            if (m_linger) {
                init();
                return true;
            }else{
                return false;
            }
        }
    }
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ... ) {
    //首先检查写缓冲区是否已满，如果已满则无法添加新的数据，直接返回 false
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );

    //将格式化的字符串写入写缓冲区中    写入的数据从 m_write_buf 的当前写入位置开始
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );

    //检查写入的字符数是否超过了写缓冲区的剩余空间，如果超过则表示无法完全写入，直接返回 false
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        va_end(arg_list);
        return false;
    }

    //更新写缓冲区的写入位置，将其移动到新添加的数据的末尾
    m_write_idx += len;

    va_end( arg_list );

    //打印添加的请求信息，然后返回 true，表示数据已成功添加到写缓冲区中
    LOG_INFO("request:%s", m_write_buf);
    return true;
}

//添加 HTTP 响应头部信息的函数

//添加 HTTP 响应的状态行
bool http_conn::add_status_line( int status, const char* title ) {
    //格式化字符串包含了 HTTP 协议版本、状态码和状态信息
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

//添加 HTTP 响应头部的各个字段
bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) && add_linger() && add_blank_line();
}

//添加 HTTP 响应头部中的 Content-Length 字段，指定响应正文的长度
bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

//添加 HTTP 响应头部中的 Connection 字段，指定连接是否保持活动状态。如果 m_linger 为 true，则设置为 "keep-alive"；否则设置为 "close"
bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

//在 HTTP 响应头部和正文之间添加一个空行，表示头部信息的结束
bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

//添加 HTTP 响应的正文内容
bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

//添加 HTTP 响应头部中的 Content-Type 字段，指定响应正文的类型为 "text/html"
bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        //如果服务器内部出现错误 (INTERNAL_ERROR)，则设置状态行为 "500 Internal Server Error"，添加相应的头部信息和错误页面内容，然后返回 false
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        //如果客户端请求错误 (BAD_REQUEST)，则设置状态行为 "400 Bad Request"，添加相应的头部信息和错误页面内容，然后返回 false
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        //如果请求的资源不存在 (NO_RESOURCE)，则设置状态行为 "404 Not Found"，添加相应的头部信息和错误页面内容，然后返回 false
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        //如果请求被拒绝 (FORBIDDEN_REQUEST)，则设置状态行为 "403 Forbidden"，添加相应的头部信息和错误页面内容，然后返回 false
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        //如果请求的是文件 (FILE_REQUEST)，则设置状态行为 "200 OK"，添加相应的头部信息。
        //如果文件大小不为 0，则设置 writev 函数需要写入的数据段。如果文件大小为 0，则返回一个空的 HTML 页面
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }else{
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string)){
                    return false;
                } 
            }
        //默认情况下，返回 false
        default:
            return false;
    }

    //设置写缓冲的首个数据段以及相关计数和待发送字节数，并返回 true
    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

//由线程池中的工作线程调用，这是处理 HTTP 请求的入口函数
void http_conn::process() {
   // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if ( read_ret == NO_REQUEST ) {
        modfd( m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    
    // 生成响应
    bool write_ret = process_write( read_ret );
    if ( !write_ret ) {
        close_coon();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}

