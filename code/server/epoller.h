#ifndef EPOLLER_H
#define EPOLLER_H

#include <sys/epoll.h> //epoll_ctl()
#include <fcntl.h>  // fcntl()
#include <unistd.h> // close()
#include <assert.h> // close()
#include <vector>
#include <errno.h>

class Epoller {
public:
    explicit Epoller(int maxEvent = 1024);

    ~Epoller();
    //添加事件
    bool AddFd(int fd, uint32_t events);
    //修改事件
    bool ModFd(int fd, uint32_t events);

    bool DelFd(int fd);
    //调用内核，让内核帮忙检测
    int Wait(int timeoutMs = -1);

    int GetEventFd(size_t i) const;

    uint32_t GetEvents(size_t i) const;
        
private:
    int epollFd_; //epoll_create创建一个epoll对象，返回值就是epollFd_，通过该描述符可以操作epoll对象

    std::vector<struct epoll_event> events_;   //检测到的事件集合
};

#endif //EPOLLER_H