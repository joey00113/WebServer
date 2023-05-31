#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <semaphore.h>
#include <thread>
#include "../log/log.h"

class SqlConnPool {
public:
    static SqlConnPool *Instance();  //单例模式，获取池的方法设为公有静态

    MYSQL *GetConn(); //获取一个连接
    void FreeConn(MYSQL * conn); // 释放数据库连接，放到池子里
    int GetFreeConnCount(); //获取空闲用户数

    void Init(const char* host, int port,
              const char* user,const char* pwd, 
              const char* dbName, int connSize);
    void ClosePool();

private:
    SqlConnPool();  //单例模式，构造函数私有化
    ~SqlConnPool();

    int MAX_CONN_; //最大连接数
    int useCount_; //当前的用户数
    int freeCount_; //空闲的用户数

    std::queue<MYSQL *> connQue_; //队列，保存当前可用的MYSQL *
    std::mutex mtx_;  //互斥锁
    sem_t semId_; //信号量
};


#endif // SQLCONNPOOL_H