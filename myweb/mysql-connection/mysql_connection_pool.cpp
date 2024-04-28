#include "../mysql-connection/mysql_connection_pool.h"

connection_pool* connection_pool::GetInstance(){
    static connection_pool connpool;
    return &connpool;
}

connection_pool::connection_pool(){
	m_CurConn = 0;
	m_FreeConn = 0;
}

void connection_pool::init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log){
    this->m_url = url;
    this->m_user = User;
    this->m_password = PassWord;
    this->m_databasename = DataBaseName;
    this->m_port = Port;
    this->m_close_log = close_log;

    // 创建MaxConn条数据库连接
    for(int i = 0; i < MaxConn; i++){
        MYSQL* con = NULL;
        con = mysql_init(con);

        if(con == NULL){
            LOG_ERROR("MYSQL Error");
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DataBaseName.c_str(), Port, NULL, 0);

        if(con == NULL){
            LOG_ERROR("MYSQL Error");
            exit(1);
        }
        // 更新连接池和空闲连接数量
        connList.push_back(con);
        ++m_FreeConn;
    }

    // 将信号量初始化为最大连接次数
    m_sem = sem(m_FreeConn);

    this->m_MaxConn = m_FreeConn;
}

//当线程数量大于数据库连接数量时，使用信号量进行同步，每次取出连接，信号量原子减1，释放连接原子加1，若连接池内没有连接了，则阻塞等待

MYSQL *connection_pool::getconnection(){   
    MYSQL* con = NULL;
    if(connList.size() == 0){
        return NULL;
    }

    //取出连接，信号量原子减1，为0则等待
    m_sem.wait();

    lock.lock();
    con = connList.front();
    connList.pop_front();

    m_FreeConn--;
    m_CurConn++;

    lock.unlock();
    return con;
}

int connection_pool::GetFreeConn(){
    return this->m_FreeConn;
}

bool connection_pool::ReleaseConnection(MYSQL *conn){
    if(conn == NULL){
        return  false;
    }
    lock.lock();

    connList.push_back(conn);
    m_CurConn--;
    m_FreeConn++;

    lock.unlock();

    m_sem.post();
    return true;
}

void connection_pool::DestroyPool(){
    lock.lock();

    if(connList.size() > 0){
        // 通过迭代器遍历，关闭数据库连接
        list<MYSQL*>::iterator it;
        for(it = connList.begin(); it != connList.end(); it++){
            MYSQL* con = *it;
            mysql_close(con);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        // 清空list
        connList.clear();
    }

    lock.unlock();
}

connection_pool::~connection_pool(){
    DestroyPool();
}

// 不直接调用获取和释放连接的接口，将其封装起来，通过RAII机制进行获取和释放

connectionRAII::connectionRAII(MYSQL** SQL, connection_pool* connpool){
    *SQL=connpool->getconnection();

    conRAII = *SQL;
    poolRAII = connpool;
}

connectionRAII::~connectionRAII(){
    poolRAII->ReleaseConnection(conRAII);
}
