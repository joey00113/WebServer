#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <mutex> //互斥锁
#include <condition_variable>//条件变量
#include <queue>//队列
#include <thread>//线程库，c++11
#include <functional>//回调

//线程池的类
class ThreadPool {
public:
    //构造函数，默认创建8个线程
    //explicit关键字，防止构造函数进行隐式转换，必须使用构造函数创建
    explicit ThreadPool(size_t threadCount = 8): pool_(std::make_shared<Pool>()) {
            assert(threadCount > 0); //断言，测试用

            //创建threadcount个子线程
            for(size_t i = 0; i < threadCount; i++) {
                //c++创建线程的方式，花括号内为每个线程要执行的代码
                std::thread([pool = pool_]{
                    std::unique_lock<std::mutex> locker(pool->mtx); //获得unique_lock锁
                    while(true) {
                        if(!pool->tasks.empty()) { //任务队列不为空
                            //从任务队列取第一个任务
                            auto task = std::move(pool->tasks.front());//返回右值引用,并将资源转移到task
                            //去掉队头的任务
                            pool->tasks.pop();
                            locker.unlock();
                            task(); //任务执行的代码，functional
                            locker.lock();
                        } 
                        else if(pool->isClosed) break; //任务队列为空，判断线程池是否关闭
                        else pool->cond.wait(locker); //任务队列为空，线程池未关闭，阻塞
                    }
                }).detach(); //设置线程分离，从thread对象分离执行的线程,允许执行独立地持续。一旦线程退出,则释放所有分配的资源。
            }
    }

    //无参构造函数，因为上面已经定义有参构造函数，编译器不会帮忙定义无参构造，假如需要调用无参构造函数，就会报错
    ThreadPool() = default;
    //移动构造函数
    ThreadPool(ThreadPool&&) = default;
    
    //析构函数
    ~ThreadPool() {
        if(static_cast<bool>(pool_)) {
            {
                std::lock_guard<std::mutex> locker(pool_->mtx);
                pool_->isClosed = true; //关闭线程池
            }
            pool_->cond.notify_all(); //唤醒所有休眠的进程
        }
    }
    //添加一个任务
    template<class F>
    void AddTask(F&& task) {//使用完美转发，根据传递进来的task类型调用相应的函数
        {
            std::lock_guard<std::mutex> locker(pool_->mtx); //互斥锁，离开作用域自动解锁
            pool_->tasks.emplace(std::forward<F>(task)); //把任务添加到任务队列
        }
        pool_->cond.notify_one(); //添加任务后，唤醒一个阻塞的线程去处理
    }

private:
//定义一个结构体，池
    struct Pool {
        std::mutex mtx;  //互斥锁
        std::condition_variable cond; //条件变量
        bool isClosed; //是否关闭
        std::queue<std::function<void()>> tasks;  //队列，保存任务
    };
    std::shared_ptr<Pool> pool_; //池，shared_ptr是引用计数型智能指针
};


#endif //THREADPOOL_H