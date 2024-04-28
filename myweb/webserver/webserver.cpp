#include "webserver.h"

WebServer::WebServer() {
    //创建 http_conn 对象
    users = new http_conn[MAX_FD];

    // root 文件夹目录
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer() {
    //关闭文件描述符
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);

    //释放对象空间
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, int opt_linger, int trigmode, 
                     int sql_num, int thread_num, int close_log, int model) {
    this->m_port = port;
    this->m_user = user;
    this->m_passWord = passWord;
    this->m_databaseName = databaseName;
    this->m_log_write = log_write;
    this->m_OPT_LINGER = opt_linger;
    this->m_TRIGMode = trigmode;
    this->m_sql_num = sql_num;
    this->m_thread_num = thread_num;
    this->m_close_log = close_log;
    this->m_model = model;
}

void WebServer::thread_pool() {
    //线程池
    m_pool = new threadpool<http_conn>(m_model, m_connpool, m_thread_num);
    //m_pool = new threadpool<http_conn>(m_thread_num, 10000, m_actormodel, m_connpool);
}

void WebServer::sql_pool() {
    //初始化数据库连接池
    m_connpool = connection_pool::GetInstance();
    m_connpool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库  读表
    users->initmysql(m_connpool);
}

void WebServer::log_write() {
    //初始化日志
    if(m_close_log == 1){
        //异步
        Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
    }else{
        //同步
        Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::trig_mode() {
    //LT + LT   （监听和连接都使用 LT 模式）
    if (m_TRIGMode == 0){
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET   （监听使用 LT 模式，连接使用 ET 模式）
    else if (m_TRIGMode == 1){
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT   （监听使用 ET 模式，连接使用 LT 模式）
    else if (m_TRIGMode == 2){
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET   （监听和连接都使用 ET 模式）
    else if (m_TRIGMode == 3){
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::eventListen() {
    //网络编程 socket

    // 创建监听套接字
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    //关闭连接
    if (m_OPT_LINGER == 0){
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }else if (m_OPT_LINGER == 1){
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    // 绑定监听套接字
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons( m_port );

    // 端口复用
    int reuse = 1;
    setsockopt( m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    ret = bind( m_listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert(ret >= 0);
    // 开始监听
    ret = listen( m_listenfd, 5 );
    assert(ret >= 0);

    // 初始化定时器
    utils.init(TIMESLOT);

    // 创建epoll对象，和事件数组，添加
    epoll_event events[ MAX_EVENT_NUMBER ];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);
    // 添加到epoll对象中
    utils.addfd( m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    // 设置 HTTP 连接类中的 epoll 文件描述符
    http_conn::m_epollfd = m_epollfd;

    // 创建管道，将管道读端添加到 epoll 对象中
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    // 添加信号处理函数，并设置定时器信号
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::eventLoop() {
    // 定义超时和停止服务器的标志
    bool timeout = false;
    bool stop_server = false;

    // 循环监听事件
    while (!stop_server){
        // 调用 epoll_wait 函数等待事件发生
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        // 如果返回值小于 0 并且错误不是中断错误，则打印错误日志并退出循环
        if (number < 0 && errno != EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        // 遍历所有事件
        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;

            //处理新到的客户连接
            if (sockfd == m_listenfd) {
                bool flag = dealclientdata();
                if (flag == false){
                    continue;
                }
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) { // 处理异常事件，包括客户端关闭连接等
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN)) {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag){
                    LOG_ERROR("%s", "dealclientdata failure");
                }
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN){
                dealwithread(sockfd);
            }else if (events[i].events & EPOLLOUT){
                dealwithwrite(sockfd);
            }
        }
        // 如果发生了超时事件
        if (timeout){
            // 处理定时器事件
            utils.timer_handler();

            // 记录定时器事件
            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}

void WebServer::timer(int connfd, sockaddr_in client_address) {
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;

    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;//回调函数

    //获取当前时间，然后将定时器的超时时间设置为当前时间加上 3 倍的 TIMESLOT，即将来三个时间单位后超时
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;

    //将新创建的定时器对象设置给对应的 users_timer，然后通过工具类中的定时器链表 m_timer 添加定时器对象
    users_timer[connfd].timer = timer;
    utils.m_timer.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer) {
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

//处理定时器超时事件
void WebServer::deal_timer(util_timer *timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]);
    //检查定时器是否存在，如果存在，则通过工具类中的定时器链表 m_timer 调用 del_timer 函数，从链表中删除定时器
    if (timer)
    {
        utils.m_timer.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclientdata() {
    // 定义客户端地址结构和其长度
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    // 根据触发模式进行不同的处理
    if (m_LISTENTrigmode == 0){
        // LT
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0){
            // 如果 accept 函数失败，则记录错误日志并返回 false
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD){
            // 如果当前连接数超过了最大连接数限制，则向客户端发送错误信息并记录错误日志，然后返回 false
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        // 如果连接数未达到最大限制，则设置定时器，并返回 true
        timer(connfd, client_address);
    }else{
        // ET
        while (1){
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0){
                // 如果 accept 函数失败，则记录错误日志并跳出循环
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD){
                // 如果当前连接数超过了最大连接数限制，则向客户端发送错误信息并记录错误日志，然后跳出循环
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            // 如果连接数未达到最大限制，则设置定时器，并继续循环等待新连接
            timer(connfd, client_address);
        }
        // 返回 false，表示处理完一个连接后继续等待下一个连接
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server) {
    int ret = 0;
    int sig;
    char signals[1024];
    // 从管道中接收信号
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1){
        // 如果接收失败，则返回 false
        return false;
    }else if (ret == 0){
        // 如果没有接收到数据，则返回 false
        return false;
    }else{
        // 遍历接收到的信号
        for (int i = 0; i < ret; ++i){
            switch (signals[i]){
                // 如果是定时器信号( SIGALRM )，则设置 timeout 标志为 true
                case SIGALRM:{
                    timeout = true;
                    break;
                }
                // 如果是停止服务器信号( SIGTERM )，则设置 stop_server 标志为 true
                case SIGTERM:{
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

// 处理读事件
void WebServer::dealwithread(int sockfd) {
    // 获取当前连接对应的定时器
    util_timer *timer = users_timer[sockfd].timer;

    //reactor
    if (m_model == 1){
        // 如果定时器存在，则调整定时器
        if (timer){
            adjust_timer(timer);
        }

        //若监测到读事件，将该事件放入请求队列
        m_pool->append_p(users + sockfd, 0);

        // 等待事件处理完成
        while (true){
            // 如果事件处理完成（标志位 improv 被置为 1）
            if (users[sockfd].improv == 1){
                // 如果定时器标志位被设置，说明连接超时，需要处理定时器
                if (users[sockfd].timer_flag == 1){
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                // 重置 improv 标志位，并跳出循环
                users[sockfd].improv = 0;
                break;
            }
        }
    }else{
        //proactor
        // 调用用户定义的读事件处理函数，处理读事件
        if (users[sockfd].read_once()){
            // 记录日志，表示正在处理客户端的读事件
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //若监测到读事件，将该事件放入请求队列
            m_pool->append(users + sockfd);

            // 如果定时器存在，则调整定时器
            if (timer){
                adjust_timer(timer);
            }
        }else{
            // 如果读事件处理失败，则处理定时器
            deal_timer(timer, sockfd);
        }
    }
}

// 处理写事件
void WebServer::dealwithwrite(int sockfd) {
    // 获取当前连接对应的定时器
    util_timer *timer = users_timer[sockfd].timer;
    //reactor
    if (m_model == 1){
        // 如果定时器存在，则调整定时器
        if (timer){
            adjust_timer(timer);
        }

        // 将写事件放入请求队列中
        m_pool->append_p(users + sockfd, 1);

        // 等待事件处理完成
        while (true){
            // 如果事件处理完成（标志位 improv 被置为 1）
            if (users[sockfd].improv == 1){
                // 如果定时器标志位被设置，说明连接超时，需要处理定时器
                if (users[sockfd].timer_flag == 1){
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                // 重置 improv 标志位，并跳出循环
                users[sockfd].improv = 0;
                break;
            }
        }
    }else{
        //proactor
        // 调用用户定义的写事件处理函数，向客户端发送数据
        if (users[sockfd].write()){
            // 记录日志，表示正在向客户端发送数据
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            // 如果定时器存在，则调整定时器
            if (timer){
                adjust_timer(timer);
            }
        }else{
            // 如果写事件处理失败，则处理定时器
            deal_timer(timer, sockfd);
        }
    }
}
