#include "epoller.h"

Epoller::Epoller(int maxEvent):epollFd_(epoll_create(512)), events_(maxEvent){
    assert(epollFd_ >= 0 && events_.size() > 0);
}

Epoller::~Epoller() {
    close(epollFd_);
}

bool Epoller::AddFd(int fd, uint32_t events) {
    if(fd < 0) 
    return false;

    //创建一个epoll_event
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    //调用epoll_ctl对epoll实例进行管理，这里是添加文件描述符信息
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
}

bool Epoller::ModFd(int fd, uint32_t events) {
    if(fd < 0) 
    return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    //调用epoll_ctl对epoll实例进行管理，这里是修改文件描述符信息
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
}

bool Epoller::DelFd(int fd) {
    if(fd < 0) 
    return false;
    epoll_event ev = {0};
    //调用epoll_ctl对epoll实例进行管理，这里是删除文件描述符信息
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &ev);
}

int Epoller::Wait(int timeoutMs) {
    //epoll_wait进行检测，返回发生更改的文件描述符个数
    return epoll_wait(epollFd_, &events_[0], static_cast<int>(events_.size()), timeoutMs);
}

int Epoller::GetEventFd(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].data.fd;
}

uint32_t Epoller::GetEvents(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].events;
}