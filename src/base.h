//
// Created by wangqi on 17-6-9.
//

#ifndef MUTILADAPTER_SAFEQUEUE_H
#define MUTILADAPTER_SAFEQUEUE_H

#include <queue>
#include <cassert>
#include <cstring>
#include "base/Logging.h"
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include "base/Mutex.h"
#include "base/Condition.h"
#include <boost/utility.hpp>
#include <map>
#include <utility>
#include "base/Thread.h"
#include <boost/bind.hpp>
#include <sstream>


template <typename T>
class thread_safe_queue
{
public:
    thread_safe_queue()
            :data_queue()
            ,mtx()
            ,data_condition(mtx)
    {}
    void wait_and_pop(T& value)
    {
        muduo::MutexLockGuard lock(mtx);
        if(data_queue.empty())
        {
            data_condition.wait();
        }
        value = data_queue.front();
        data_queue.pop();
    }
    boost::shared_ptr<T> wait_and_pop()
    {
        muduo::MutexLockGuard lock(mtx);
        if(data_queue.empty())
        {
            data_condition.wait();
        }
        boost::shared_ptr<T> res = data_queue.front();
        data_queue.pop();
        return res;
    }
    bool try_pop(T& value)
    {
        muduo::MutexLockGuard lock(mtx);
        if(data_queue.empty())
            return false;
        value = data_queue.front();
        data_queue.pop();
        return true;
    }
    boost::shared_ptr<T> try_pop()
    {
        muduo::MutexLockGuard lock(mtx);
        if(data_queue.empty())
            return boost::shared_ptr<T>();
        boost::shared_ptr<T> res = data_queue.front();
        data_queue.pop();
        return res;
    }
    void push(T value)
    {
        boost::shared_ptr<T> data(boost::make_shared<T>(value));
        muduo::MutexLockGuard lock(mtx);
        data_queue.push(data);
        data_condition.notifyAll();
    }
    int size()
    {
        muduo::MutexLockGuard lock(mtx);
        return data_queue.size();
    }
    void eraser(int count)
    {
        muduo::MutexLockGuard lock(mtx);
        while(data_queue.size() > count)
        {
            data_queue.pop();
        }
    }
private:
    std::queue<boost::shared_ptr<T> > data_queue;
    muduo::Condition data_condition;
    muduo::MutexLock mtx;
};

class Count:public boost::noncopyable
{
public:
    static Count* getInstance();
    void increment(std::string key);
    void decrement(std::string key);
    int get(std::string key);
    int getAndSet(std::string key,int value);
private:
    Count();
    class Delete
    {
    public:
        ~Delete();
    };
private:
    static Count* count;
    static Delete deleter;
    std::map<std::string,int> values;
    muduo::MutexLock mtx;
};


typedef struct Thread_local_msg
{
    int Handler;
    int Link_max_num;
    unsigned short  Port;      /*端口号*/
    int  SessionId;            /*消息ID*/
    int  Link_Num;   /*链接次数*/
}Thread_local_msg;



class thread_safe_local_msg_queue
{
public:
    thread_safe_local_msg_queue(thread_safe_queue<std::pair<uint32_t,boost::shared_ptr<char> > >& msg_source);
    ~thread_safe_local_msg_queue();
    void add_ip(const std::string& ip,Thread_local_msg& value);
    void delete_ip(const std::string& ip);
    std::string get_buff_info(std::ostringstream& os);

private:
    void dispatcher_msg();
    void send_msg_peer();
    boost::shared_ptr<std::pair<uint32_t , boost::shared_ptr<char> > > try_pop(const std::string& ip);
    void eraser(const std::string& ip,int num);

private:
    typedef std::pair<Thread_local_msg,std::queue<boost::shared_ptr< std::pair<uint32_t,boost::shared_ptr<char> > > > > msg_value;
    typedef std::map<std::string,msg_value>::iterator thread_local_msg_queue_iter;
    typedef thread_safe_queue<std::pair<uint32_t,boost::shared_ptr<char> > > msg_source;
    bool dispatcher_msg_thread_start;
    bool send_msg_start;
    muduo::MutexLock mtx;
    muduo::Condition thread_local_msg_queue_condition;
    std::map<std::string,msg_value>  thread_local_msg_queue;
    msg_source& msg_queue;
    muduo::Thread Send_msg_peer_thread;
    muduo::Thread dispatcher_msg_thread;
};


#endif //MUTILADAPTER_SAFEQUEUE_H
