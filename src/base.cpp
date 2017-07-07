//
// Created by wangqi on 17-7-1.
//

#include "base.h"
#include "BATNetSDKRawAPI.h"


Count::Count()
        :values()
        ,mtx()
{}

Count* Count::getInstance()
{
    if(NULL == count)
        count = new Count();
    return count;
}
void Count::increment(std::string key)
{
    muduo::MutexLockGuard lock(mtx);
    values[key]++;
}

void Count::decrement(std::string key)
{
    muduo::MutexLockGuard lock(mtx);
    values[key]--;
    if(values[key] <= 0)
        values[key] = 0;
}

int Count::get(std::string key)
{
    muduo::MutexLockGuard lock(mtx);
    return values[key];
}

int Count::getAndSet(std::string key,int value)
{
    muduo::MutexLockGuard lock(mtx);
    int temp = values[key];
    values[key] = value;
    return temp;
}

Count::Delete::~Delete()
{
    if(Count::count)
        delete(Count::count);
}

Count* Count::count = NULL;




thread_safe_local_msg_queue::thread_safe_local_msg_queue(thread_safe_queue<std::pair<uint32_t,boost::shared_ptr<char> > >& msg_source)
                    :dispatcher_msg_thread_start(true)
                    ,send_msg_start(true)
                    ,mtx()
                    ,thread_local_msg_queue_condition(mtx)
                    ,thread_local_msg_queue()
                    ,msg_queue(msg_source)
                    ,Send_msg_peer_thread(boost::bind(&thread_safe_local_msg_queue::send_msg_peer,this),"Send_msg_peer_thread")
                    ,dispatcher_msg_thread(boost::bind(&thread_safe_local_msg_queue::dispatcher_msg,this),"dispatcher_msg_thread")
{
    Send_msg_peer_thread.start();
    dispatcher_msg_thread.start();
}

thread_safe_local_msg_queue::~thread_safe_local_msg_queue()
{
    dispatcher_msg_thread_start = false;
    send_msg_start = false;
    Send_msg_peer_thread.join();
    dispatcher_msg_thread.join();
}

void thread_safe_local_msg_queue::add_ip(const std::string &ip, Thread_local_msg &value)
{
    muduo::MutexLockGuard lock(mtx);
    thread_local_msg_queue_iter it = thread_local_msg_queue.find(ip);
    if(thread_local_msg_queue.end() != it)
    {
        if(it->second.first.Link_Num < it->second.first.Link_max_num)
        {
            it->second.first.Link_Num++;
            LOG_INFO << "ip => " << ip << "\thas connected\t" << it->second.first.Link_Num << "\ttimes";
        }
        else
        {
            LOG_INFO << "ip => " << ip << "\thas connected\t" << it->second.first.Link_Num + 1 << "\ttimes,over 3 times.";
            BATNetSDKRaw_Disconnect(it->second.first.Handler,it->second.first.SessionId);
            thread_local_msg_queue.erase(ip);
        }
    }
    else
    {
        thread_local_msg_queue[ip].first.Port = value.Port;
        thread_local_msg_queue[ip].first.SessionId = value.SessionId;
        thread_local_msg_queue[ip].first.Handler = value.Handler;
        thread_local_msg_queue[ip].first.Link_max_num = value.Link_max_num;
        thread_local_msg_queue[ip].first.Link_Num = value.Link_Num + 1;
        thread_local_msg_queue_condition.notifyAll();
        LOG_INFO << "ip => " << ip << "\thas connected\t" << thread_local_msg_queue[ip].first.Link_Num << "\ttimes";
    }
}

void thread_safe_local_msg_queue::delete_ip(const std::string &ip)
{
    muduo::MutexLockGuard lock(mtx);
    thread_local_msg_queue.erase(ip);
    LOG_INFO << "ip => " << ip << "\twill be closed due to disconnected senddataserver";
}

void thread_safe_local_msg_queue::eraser(const std::string& ip,int num)
{
    muduo::MutexLockGuard lock(mtx);
    msg_value msg = thread_local_msg_queue[ip];
    int capacity = msg.second.size();
    while (capacity > num)
    {
        msg.second.pop();
    }
}


boost::shared_ptr<std::pair<uint32_t , boost::shared_ptr<char> > > thread_safe_local_msg_queue::try_pop(
        const std::string &ip)
{
    muduo::MutexLockGuard lock(mtx);
    if(thread_local_msg_queue[ip].second.empty())
        return boost::shared_ptr<std::pair<uint32_t , boost::shared_ptr<char> > >();
    boost::shared_ptr<std::pair<uint32_t , boost::shared_ptr<char> > > res = thread_local_msg_queue[ip].second.front();
    thread_local_msg_queue[ip].second.pop();
    return res;
}

void thread_safe_local_msg_queue::dispatcher_msg()
{
    while(dispatcher_msg_thread_start)
    {
        boost::shared_ptr<std::pair<uint32_t , boost::shared_ptr<char> > > msg = msg_queue.wait_and_pop();
        if (msg != NULL)
        {
            muduo::MutexLockGuard lock(mtx);
            if(thread_local_msg_queue.empty())
            {
                thread_local_msg_queue_condition.wait();
            }
            thread_local_msg_queue_iter start = thread_local_msg_queue.begin();
            while (start != thread_local_msg_queue.end())
            {
                thread_local_msg_queue[start->first].second.push(msg);
                ++start;
            }
        }
    }
}


void thread_safe_local_msg_queue::send_msg_peer()
{
    while(send_msg_start)
    {
        {
            muduo::MutexLockGuard lock(mtx);
            if(thread_local_msg_queue.empty())
            {
                thread_local_msg_queue_condition.wait();
            }
        }
        for(thread_local_msg_queue_iter it = thread_local_msg_queue.begin();it != thread_local_msg_queue.end();++it)
        {
            if(it->second.second.size() > 1000000)
            {
                LOG_INFO <<"client => "<<it->first <<"\tport => "<< it->second.first.Port << "\treceive data too slowly, here will drop a large count of data about 1000000 messages";
                eraser(it->first,50000);
            }
            boost::shared_ptr<std::pair<uint32_t , boost::shared_ptr<char> > > msg = try_pop(it->first);
            if(msg != NULL)
            {
                BATNetSDKRaw_Send(it->second.first.Handler,it->second.first.SessionId,(*msg).second.get(),(*msg).first);
            }
        }
    }
}


std::string thread_safe_local_msg_queue::get_buff_info(std::ostringstream& os)
{
    std::string end_flag = "pxb_dataguard\r\n";
    muduo::MutexLockGuard lock(mtx);
    thread_local_msg_queue_iter start = thread_local_msg_queue.begin();
    while(start != thread_local_msg_queue.end())
    {
        os << "IP => " << start->first <<"  port => " << start->second.first.Port << "\tqueue_buf size => " << start->second.second.size() << "\n";
        ++start;
    }
    os << end_flag;
}































