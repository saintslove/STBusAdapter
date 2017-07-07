//
// Created by wangqi on 17-6-7.
//

#ifndef MUTILADAPTER_MUTILADAPTER_H
#define MUTILADAPTER_MUTILADAPTER_H

#include "BATNetSDKRawAPI.h"
#include <string>
#include <vector>
#include <map>
#include "xmlParser.h"
#include "base/Timestamp.h"
#include "base/TimeZone.h"
#include "base/Mutex.h"
#include "base/Atomic.h"
#include "base.h"
#include "base/Condition.h"
#include "base/Thread.h"
//#include <boost/tuple/tuple.hpp>
#include <utility>



using namespace muduo;

class STBusAdapter {

private:
    typedef struct timebucket
    {
        int sessionID;
        bool closed;
        struct tm connect_establish;
        struct tm connect_close;
    }connnect_timebucket;

    typedef struct Map_Monitor
    {
        unsigned short Port;   /*端口号*/
        int    SessionID;      /*消息ID*/
        bool   Verification;   /*验证通过标志*/
        bool   Authority;      /*权限标志*/
    }Map_Monitor;

    typedef struct Map_RecvData
    {
        int  SessionID;     /*消息ID*/
        int  Link_Sataus;   /*链接状态*/
        bool Send_Init_Msg; /*是否已发送初始化消息标志*/
    }Map_RecvData;

private:

    const std::string _config_path;
    bool initMsgSending;
    int RecvDataHandler;
    int SendDataHandler;
    int MonitorHandler;
    muduo::MutexLock timebuckets_lock;
    muduo::MutexLock Map_Monitor_hasConnectrd_lock;
    muduo::MutexLock list_map_lock;
    muduo::MutexLock map_conf_lock;
    muduo::MutexLock Map_RecvData_hasConnected_lock;
    muduo::Condition Map_RecvData_hasConnected_Condition;
    struct tm  day_time;
    struct tm RecvDataServerSatrtTime;
    muduo::Thread loadconfig_thread;
    muduo::Thread sendInitMsg_thread;



    typedef std::map<std::string,std::vector<std::string> > Map_list;
    typedef std::map<std::string,std::string> Map_Conf;
    typedef std::map<std::string,Map_Monitor > Map_Monitor_hasConnectrd;
    typedef Map_Monitor_hasConnectrd::iterator Map_Monitor_hasConnectrd_Iter;
    typedef Map_Monitor_hasConnectrd::value_type Map_Monitor_hasConnectrd_type;
    typedef std::map<std::string,Map_RecvData > Map_RecvData_hasConnected;
    typedef Map_RecvData_hasConnected::value_type Map_RecvData_hasConnected_type;
    typedef Map_RecvData_hasConnected::iterator Map_RecvData_hasConnected_Iter;
    typedef std::vector<connnect_timebucket>::iterator timebuckets_Iter;

    Map_list list_map;
    Map_Conf map_conf;
    Count* map_count;
    Map_Monitor_hasConnectrd map_monitor_connected;
    Map_RecvData_hasConnected map_RecvData_hasConnected;
    std::vector<connnect_timebucket> timebuckets;
    thread_safe_queue<std::pair<uint32_t,boost::shared_ptr<char> > > msg_queue;
    thread_safe_local_msg_queue local_msg_queue;


private:

    template <typename T>
    struct Deleter
    {
        void operator() (T* item)
        {
            if(item != NULL)
                delete[](item);
            item = NULL;
        }
    };

    template <typename T,typename T2,bool find_ip>
    struct find_item
    {
        find_item(T2 t):_t(t) {}
        find_item():_t() {}
        bool operator() (T item)
        {
            return strcmp(_t.c_str(),item.c_str()) == 0;
        }
    private:
        T2 _t;
    };

    template <bool find_ip>
    struct find_item<std::string,std::string,find_ip>
    {
        find_item(std::string t):_t(t) {}
        find_item():_t() {}
        bool operator() (std::string item)
        {
            return strcmp(_t.c_str(),item.c_str()) == 0;
        }
    private:
        std::string _t;
    };

    template <bool find_ip>
    struct find_item<Map_RecvData_hasConnected_type,std::string,find_ip>
    {
        find_item(std::string t):_t(t) {}
        find_item():_t() {}
        bool operator() (Map_RecvData_hasConnected_type item)
        {
            if(find_ip)
                return strcmp(_t.c_str(),item.first.c_str()) == 0;
            else
                return ((!item.second.Link_Sataus) && (!item.second.Send_Init_Msg));
        }
    private:
        std::string _t;
    };

    template <bool find_ip>
    struct find_item<Map_Monitor_hasConnectrd_type,int,find_ip>
    {
        find_item(int t):_t(t) {}
        find_item():_t() {}
        bool operator() (Map_Monitor_hasConnectrd_type item)
        {
            return item.second.SessionID == _t;;
        }
    private:
        int _t;
    };

    template <bool find_ip>
    struct find_item<Map_Monitor_hasConnectrd_type,std::string,find_ip>
    {
        find_item(std::string t):_t(t) {}
        find_item():_t() {}
        bool operator() (Map_Monitor_hasConnectrd_type item)
        {
            return strcmp(_t.c_str(),item.first.c_str()) == 0;
        }
    private:
        std::string _t;
    };

public:
    STBusAdapter(const std::string& config_path,int nDevType, const std::string& strDevSN);
    ~STBusAdapter();
    int StartRecvDataServer();
    int StartSendDataServer();
    int StartMonitorServer();

private:
    void LoadConfig();
    void Insert(XMLNode& node,Map_list& map_list);
    static int OnRecvDatahand(int sessionId, const char* buf, int len, void* userdata);
    static int OnRecvDataConnectCallback(int sessionId, int status, const char* ip, unsigned short port, void* userdata);
    static int OnSendDatahand(int sessionId, const char* buf, int len, void* userdata);
    static int OnSendDataConnectCallback(int sessionId, int status, const char* ip, unsigned short port, void* userdata);
    static int OnMonitorDatahand(int sessionId, const char* buf, int len, void* userdata);
    static int OnMonitorDataConnectCallback(int sessionId, int status, const char* ip, unsigned short port, void* userdata);
    int RecvDatahand(int sessionId, const char* buf, int len, void* userdata);
    int RecvDataConnectCallback(int sessionId, int status, const char* ip, unsigned short port, void* userdata);
    int SendDatahand(int sessionId, const char* buf, int len, void* userdata);
    int SendDataConnectCallback(int sessionId, int status, const char* ip, unsigned short port, void* userdata);
    int MonitorDatahand(int sessionId, const char* buf, int len, void* userdata);
    int MonitorDataConnectCallback(int sessionId, int status, const char* ip, unsigned short port, void* userdata);
    void FormatMsg(uint16_t& msg_type, const char* source_data,uint32_t source_len,char* dest,uint32_t& dest_len);
    void InitMsg(boost::shared_ptr<char>& buf);
    void Count_day_misunderstand();
    void Count_day_total_count();
    void Getline(const char* buf,std::string& cmd,int buflen,int& len);
    void GetCommand(const char* buf,std::vector<std::string>& cmd,int buflen,int& len);
    int SendInitMsg();
};


#endif //MUTILADAPTER_MUTILADAPTER_H
