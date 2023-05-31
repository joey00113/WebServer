#include "webserver.h"

using namespace std;

WebServer::WebServer(
            int port, int trigMode, int timeoutMS, bool OptLinger,
            int sqlPort, const char* sqlUser, const  char* sqlPwd,
            const char* dbName, int connPoolNum, int threadNum,
            bool openLog, int logLevel, int logQueSize):
            port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
            timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller()){
    //  /home/joey/WebServer-master/resources/为服务器资源的根目录   
    srcDir_ = getcwd(nullptr, 256);  //获取当前工作路径的名称，传递nullptr就直接返回指针指向地址
    assert(srcDir_);
    strncat(srcDir_, "/resources/", 16); //拼接当前路径和/resources/
    
    //初始化客户端连接类的静态变量，设置连接数为0和资源目录
    HttpConn::userCount = 0;
    HttpConn::srcDir = srcDir_;

    //初始化mysql连接池，单例模式，唯一实例，局部静态变量方法，生命周期为程序运行期
    //只要调用Instance()方法就可以访问得到这个唯一实例
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);
    
    //初始化事件的模式，ET模式，main中设置为3
    InitEventMode_(trigMode); 

    //初始化套接字
    if(!InitSocket_()){ 
        isClose_ = true; //初始化套接字不成功，关闭服务器
    }

    if(openLog) {
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if(isClose_) { LOG_ERROR("========== Server init error!=========="); }
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

//析构函数
WebServer::~WebServer() {
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

//设置监听的文件描述符和通信的文件描述符的模式
void WebServer::InitEventMode_(int trigMode) {
    listenEvent_ = EPOLLRDHUP; //监听事件，EPOLLRDHUP检测对方是否正常关闭
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;//连接事件，数据读取，设置为oneshot，一个socket只能同时被一个线程操作
    switch (trigMode){
    case 0:  //0默认用上面的事件
        break;
    case 1:   //1则连接设为ET模式
        connEvent_ |= EPOLLET;
        break;
    case 2:  //2则监听设为ET模式
        listenEvent_ |= EPOLLET;
        break;
    case 3: //3则监听和连接都是ET模式
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    default:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET); //设置客户端的ET模式开关
}

//启动函数
void WebServer::Start() {
    int timeMS = -1;  /* epoll wait timeout == -1 无事件将阻塞，0代表不阻塞 */
    if(!isClose_){ LOG_INFO("========== Server start =========="); }
    //主线程，只要不是处在关闭状态，就一直调用epollwait
    while(!isClose_) {
        if(timeoutMS_ > 0) {
            timeMS = timer_->GetNextTick(); //设定阻塞时间为到达下一个超时时间的时间长度
        }
        //调用epoll_wait，返回发生变化的文件描述符的个数
        int eventCnt = epoller_->Wait(timeMS); //设定阻塞时间，减少epollwait调用次数

         /* 遍历处理事件 */
        for(int i = 0; i < eventCnt; i++) {
            //从events_数组中获取发生了变化的文件描述符的信息
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);

            //若返回的文件描述符与监听的文件描述符一致，说明监听的描述符有数据，代表有新连接，处理连接事件
            if(fd == listenFd_) {
                DealListen_(); //接受客户端连接
            }

            //文件描述符不是监听的描述符，是通信的描述符
            //连接出现了错误，关闭连接或者正常关闭连接
            //users_是一个map，键为客户端socket的文件描述符，值为httpcoon对象
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]); //关闭连接
            }
            //通信描述符，读事件发生
            else if(events & EPOLLIN) {
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]); //处理读操作
            }
            else if(events & EPOLLOUT) {
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]); //处理写操作
            } 
            else {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

void WebServer::SendError_(int fd, const char*info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if(ret < 0) {
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

void WebServer::CloseConn_(HttpConn* client) {
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());
    client->Close();
}
//添加客户端
void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd > 0);
    //创建一个新的httpconn的对象，进行初始化
    //将连接对象添加到map集合
    users_[fd].init(fd, addr);
    if(timeoutMS_ > 0) {
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }
    //将新连接的fd添加到epoll对象，即在epoll内核事件表注册新连接客户端的读写事件，监听是否有数据到达
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    SetFdNonblock(fd); //设置非阻塞，
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

//处理监听事件，有新的客户端连接
void WebServer::DealListen_() {
    struct sockaddr_in addr; //保存连接的客户端的信息
    socklen_t len = sizeof(addr);
    do{
        //非阻塞模式，不会死循环，因为没有新的客户端连接后，accept会返回-1
        //accept会创建一个新的通信socket，返回创建的套接字的文件描述符
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);
        if(fd <= 0){
            return;
        }
        //连接成功
        else if(HttpConn::userCount >= MAX_FD) {//超出当前最大连接数量
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        //添加客户端
        AddClient_(fd, addr);
    }while(listenEvent_ & EPOLLET);//对于ET模式，需要一次性连接
}

void WebServer::DealRead_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);//发生读事件，延长超时时间
    //在线程池的任务队列中添加任务，reactor模式读取数据交由子线程处理
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client));
}

void WebServer::DealWrite_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);//发生写事件，延长超时时间
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}

void WebServer::ExtentTime_(HttpConn* client) {
    assert(client);
    if(timeoutMS_ > 0) { timer_->adjust(client->GetFd(), timeoutMS_); }
}

//在子线程中执行读事件
void WebServer::OnRead_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno);//读取客户端的数据,读到客户端对象的读缓冲区
    if(ret <= 0 && readErrno != EAGAIN) { //发生错误或读取结束
        CloseConn_(client);
        return;
    }
    //业务逻辑的处理
    OnProcess(client); 
}

//处理业务逻辑实际上就是处理HTTP请求
void WebServer::OnProcess(HttpConn* client) {
    //处理业务逻辑成功，修改文件描述符为可写，向epoll示例注册写事件，此时主线程一直在wait
    //当主线程监听到可写，就会进行写事件处理
    if(client->process()){
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    } 
    else{
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

//在子线程中执行写事件
void WebServer::OnWrite_(HttpConn* client) {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno);
    if(client->ToWriteBytes() == 0) {
        /* 传输完成 */
        if(client->IsKeepAlive()) {
            OnProcess(client);
            return;
        }
    }
    else if(ret < 0) {
        if(writeErrno == EAGAIN) {
            /* 继续传输 */
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    CloseConn_(client);
}

//初始化套接字
/* Create listenFd */
bool WebServer::InitSocket_() {
    int ret;
    struct sockaddr_in addr; //套接字地址
    if(port_ > 65535 || port_ < 1024) {
        LOG_ERROR("Port:%d error!",  port_);
        return false;
    }
    addr.sin_family = AF_INET; //协议族为ipv4
    //INADDR_ANY表示绑定任何可以绑定的ip地址
    addr.sin_addr.s_addr = htonl(INADDR_ANY);//把ip地址由主机字节序转换为网络字节序
    addr.sin_port = htons(port_); //把端口号由主机字节序转换为网络字节序
    
    struct linger optLinger = { 0 };
    if(openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }
    //创建一个socket
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listenFd_ < 0) {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if(ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    int optval = 1;
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }
    //绑定socket
    ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));
    if(ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }
    //监听
    ret = listen(listenFd_, 6);
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }
    //将监听的文件描述符添加到epoll管理
    ret = epoller_->AddFd(listenFd_,  listenEvent_ | EPOLLIN);
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    //设置监听文件描述符为非阻塞
    SetFdNonblock(listenFd_);
    LOG_INFO("Server port:%d", port_);
    return true;
}

//设置文件描述符为非阻塞
int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    //int flag = fcntl(fd, F_GETFD, 0)
    // flag = flag | O_NONBLOCK
    // fcntl(fd, F_SETFL, flag) 
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}


