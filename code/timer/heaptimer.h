#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <queue>
#include <unordered_map>
#include <time.h>
#include <algorithm>
#include <arpa/inet.h> 
#include <functional> 
#include <assert.h> 
#include <chrono>
#include "../log/log.h"

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

struct TimerNode {
    int id; //文件描述符
    TimeStamp expires; //超时时间
    TimeoutCallBack cb; //回调
    bool operator<(const TimerNode& t){ //运算符重载，比较超时时间
        return expires < t.expires;
    }
};

class HeapTimer {
public:
    HeapTimer() { heap_.reserve(64); }

    ~HeapTimer() { clear(); }
    
    void adjust(int id, int newExpires);

    void add(int id, int timeOut, const TimeoutCallBack& cb);

    void doWork(int id);

    void clear();

    void tick();

    void pop();

    int GetNextTick();

private:
    void del_(size_t i);  //删除元素
    
    void siftup_(size_t i); //上升

    bool siftdown_(size_t index, size_t n); //下沉

    void SwapNode_(size_t i, size_t j);

    std::vector<TimerNode> heap_; //数据类型为timernode结构体的堆

    std::unordered_map<int, size_t> ref_; //键：文件描述符，值：堆的索引
};

#endif //HEAP_TIMER_H